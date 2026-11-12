#include <stdint.h>

#include "gdt.h"
#include "kstring.h"
#include "terminal.h"

#define GDT_ENTRY_COUNT 6

struct gdt_entry
{
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity; // [flags:4][limit_high:4]
    uint8_t base_high;
} __attribute__((packed));

struct gdt_pointer
{
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct tss_entry
{
    uint32_t prev_tss;
    uint32_t esp0; // stack kernel yang dipakai CPU saat Ring 3 -> Ring 0
    uint32_t ss0;
    uint32_t esp1, ss1, esp2, ss2;
    uint32_t cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base; // >= batas TSS: tidak ada I/O bitmap -> Ring 3 tidak boleh in/out
} __attribute__((packed));

static struct gdt_entry gdt[GDT_ENTRY_COUNT];
static struct gdt_pointer gdt_ptr;
static struct tss_entry tss;

extern void gdt_flush(uint32_t gdt_pointer_address);
extern void tss_flush(void);

static void gdt_set(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags)
{
    gdt[i].limit_low = (uint16_t)(limit & 0xFFFFu);
    gdt[i].base_low = (uint16_t)(base & 0xFFFFu);
    gdt[i].base_mid = (uint8_t)((base >> 16) & 0xFFu);
    gdt[i].access = access;
    gdt[i].granularity = (uint8_t)(((limit >> 16) & 0x0Fu) | (uint32_t)(flags << 4));
    gdt[i].base_high = (uint8_t)((base >> 24) & 0xFFu);
}

void gdt_initialize(void)
{
    // flags 0xC = granularity 4 KiB + segmen 32-bit. Semua segmen "flat" (base 0, limit 4 GiB);
    // proteksi memori dilakukan paging, bukan segmentasi.
    gdt_set(0, 0, 0, 0x00, 0x0);       // null
    gdt_set(1, 0, 0xFFFFF, 0x9A, 0xC); // kernel code: present, DPL0, exec/read
    gdt_set(2, 0, 0xFFFFF, 0x92, 0xC); // kernel data: present, DPL0, read/write
    gdt_set(3, 0, 0xFFFFF, 0xFA, 0xC); // user code:   present, DPL3, exec/read
    gdt_set(4, 0, 0xFFFFF, 0xF2, 0xC); // user data:   present, DPL3, read/write

    memset(&tss, 0, sizeof(tss));
    tss.ss0 = KERNEL_DATA_SEL;
    tss.esp0 = 0; // diisi scheduler sebelum thread user pertama jalan
    tss.iomap_base = (uint16_t)sizeof(tss);
    gdt_set(5, (uint32_t)(uintptr_t)&tss, sizeof(tss) - 1u, 0x89, 0x0); // TSS 32-bit available

    gdt_ptr.limit = (uint16_t)(sizeof(gdt) - 1u);
    gdt_ptr.base = (uint32_t)(uintptr_t)gdt;

    gdt_flush((uint32_t)(uintptr_t)&gdt_ptr);
    tss_flush();
}

void tss_set_kernel_stack(uint32_t esp0)
{
    tss.esp0 = esp0;
}

void gdt_dump(void)
{
    static const char *names[GDT_ENTRY_COUNT] =
        {"null", "kernel code", "kernel data", "user code", "user data", "TSS"};
    uint32_t cs;

    __asm__ volatile("mov %%cs, %0" : "=r"(cs));
    cs &= 0xFFFFu;

    terminal_write("Current CS=");
    terminal_write_hex(cs);
    terminal_write(" -> CPL ");
    terminal_write_dec(cs & 3u);
    terminal_write((cs & 3u) == 0 ? " (kernel mode)\n" : " (user mode)\n");

    terminal_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    terminal_write("SELECTOR    DPL TYPE         BASE        LIMIT\n");
    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);

    for (int i = 0; i < GDT_ENTRY_COUNT; i++)
    {
        uint32_t base = gdt[i].base_low | ((uint32_t)gdt[i].base_mid << 16) | ((uint32_t)gdt[i].base_high << 24);
        uint32_t limit = gdt[i].limit_low | ((uint32_t)(gdt[i].granularity & 0x0Fu) << 16);

        if (gdt[i].granularity & 0x80u)
            limit = (limit << 12) | 0xFFFu;

        terminal_write_hex((uint32_t)(i * 8));
        terminal_write("  ");
        terminal_write_dec((gdt[i].access >> 5) & 3u);
        terminal_write("   ");
        terminal_write_pad(names[i], 13);
        terminal_write_hex(base);
        terminal_write("  ");
        terminal_write_hex(limit);
        terminal_write("\n");
    }
    terminal_write("TSS: ss0=");
    terminal_write_hex(tss.ss0);
    terminal_write(" esp0=");
    terminal_write_hex(tss.esp0);
    terminal_write("\n");
}