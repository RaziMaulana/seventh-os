#include <stddef.h>
#include <stdint.h>

#include "cmd_memory.h"
#include "heap.h"
#include "kstring.h"
#include "multiboot.h"
#include "paging.h"
#include "pmm.h"
#include "terminal.h"

static void title(const char *text)
{
    terminal_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    terminal_write(text);
    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
}

static void kv(const char *key, uint32_t value)
{
    terminal_write(key);
    terminal_write("=");
    terminal_write_dec(value);
    terminal_write("  ");
}

void cmd_memmap(void)
{
    multiboot_print_memory_map();
}

void cmd_meminfo(void)
{
    heap_stats_t h;
    heap_get_stats(&h);

    title("[PMM] Physical memory (KiB)\n");
    terminal_write("  ");
    kv("usable", (uint32_t)(pmm_get_total_pages() * 4u));
    kv("free", (uint32_t)(pmm_get_free_pages() * 4u));
    kv("used", (uint32_t)(pmm_get_used_pages() * 4u));
    terminal_write("\n  highest RAM address: ");
    terminal_write_hex((uint32_t)pmm_get_highest_address());

    title("\n[PAGING]\n");
    terminal_write("  status: ");
    terminal_write(paging_is_enabled() ? "ENABLED" : "DISABLED");
    terminal_write("  identity map: 0x00000000 - ");
    terminal_write_hex(paging_get_identity_limit());

    title("\n[HEAP]\n");
    terminal_write("  virtual: ");
    terminal_write_hex(h.heap_start);
    terminal_write(" - ");
    terminal_write_hex(h.heap_start + h.total_bytes);
    terminal_write("\n  bytes   ");
    kv("mapped", h.total_bytes);
    kv("used", h.used_bytes);
    kv("free", h.free_bytes);
    kv("headers", h.overhead_bytes);
    terminal_write("\n  blocks  ");
    kv("used", h.used_blocks);
    kv("free", h.free_blocks);
    kv("largest_free", h.largest_free);
    terminal_write("\n  counters ");
    kv("alloc", h.alloc_count);
    kv("free", h.free_count);
    kv("failed", h.failed_count);
    terminal_write("\n");
}

void cmd_heapinfo(void)
{
    heap_dump();
}

// Demonstrasi fragmentasi + coalescing + integritas data.
void cmd_kmtest(void)
{
    void *block[8];
    heap_stats_t st;

    for (int i = 0; i < 8; i++) {
        block[i] = kmalloc(100);
        if (block[i] == NULL) {
            terminal_write("kmtest: kmalloc FAILED\n");
            return;
        }
        memset(block[i], 0xA0 + i, 100);
    }
    terminal_write("8 blocks allocated (100 B each). Freeing odd blocks...\n");

    for (int i = 1; i < 8; i += 2)
        kfree(block[i]);

    heap_get_stats(&st);
    terminal_write("Fragmented: ");
    kv("free_blocks", st.free_blocks);
    kv("largest_free", st.largest_free);
    kv("total_free", st.free_bytes);
    terminal_write("\n");

    int ok = 1;
    for (int i = 0; i < 8; i += 2) {
        const uint8_t *p = block[i];
        for (int j = 0; j < 100; j++)
            if (p[j] != (uint8_t)(0xA0 + i))
                ok = 0;
    }
    terminal_write(ok ? "Data integrity: OK\n" : "Data integrity: CORRUPTED\n");

    for (int i = 0; i < 8; i += 2)
        kfree(block[i]);

    heap_get_stats(&st);
    terminal_write("After freeing all: ");
    kv("free_blocks", st.free_blocks);
    terminal_write(st.free_blocks == 1 ? "(coalescing OK)\n" : "(coalescing FAILED)\n");
}

// Habiskan heap sampai kmalloc mengembalikan NULL, lalu pulihkan.
void cmd_oomtest(void)
{
    void *head = NULL;
    uint32_t count = 0;

    terminal_write("Allocating 256 KiB chunks until out of memory...\n");
    for (;;) {
        void *p = kmalloc(256u * 1024u);
        if (p == NULL)
            break;
        *(void **)p = head;                  // rantai blok di dalam blok itu sendiri
        head = p;
        count++;
    }

    terminal_write("kmalloc returned NULL after ");
    terminal_write_dec(count);
    terminal_write(" allocations (");
    terminal_write_dec(count * 256u);
    terminal_write(" KiB). Kernel is still alive.\n");

    while (head != NULL) {
        void *next = *(void **)head;
        kfree(head);
        head = next;
    }

    void *probe = kmalloc(64);
    terminal_write(probe != NULL ? "Recovery after free: OK\n" : "Recovery: FAILED\n");
    kfree(probe);
}

// Habiskan halaman fisik di PMM, lalu kembalikan semuanya.
void cmd_pmmtest(void)
{
    size_t before = pmm_get_free_pages();
    uintptr_t head = 0;
    uint32_t count = 0;

    for (;;) {
        uintptr_t page = pmm_alloc_page();
        if (page == 0)
            break;
        *(uintptr_t *)page = head;           // identity map: alamat fisik bisa diakses langsung
        head = page;
        count++;
    }

    terminal_write("PMM exhausted after ");
    terminal_write_dec(count);
    terminal_write(" pages; free now = ");
    terminal_write_dec((uint32_t)pmm_get_free_pages());
    terminal_write("\n");

    while (head != 0) {
        uintptr_t next = *(uintptr_t *)head;
        pmm_free_page(head);
        head = next;
    }

    terminal_write(pmm_get_free_pages() == before
                   ? "All pages returned: OK\n"
                   : "Page accounting MISMATCH\n");
}

// Bukti translasi virtual -> fisik: dua alamat berbeda, satu memori.
void cmd_pagetest(void)
{
    const uint32_t virt = 0xE0000000u;
    uintptr_t phys = pmm_alloc_page();

    if (phys == 0 ||
        paging_map_page(virt, (uint32_t)phys, PAGE_PRESENT | PAGE_WRITE) != 0) {
        terminal_write("pagetest: mapping FAILED\n");
        return;
    }

    *(volatile uint32_t *)virt = 0xCAFEBABEu;           // tulis lewat alamat virtual
    uint32_t via_phys = *(volatile uint32_t *)phys;     // baca lewat identity (fisik)

    uint32_t translated = 0;
    int mapped = paging_translate(virt, &translated);

    terminal_write("virtual  ");
    terminal_write_hex(virt);
    terminal_write(" -> physical ");
    terminal_write_hex(translated);
    terminal_write("\nvalue via physical alias: ");
    terminal_write_hex(via_phys);
    terminal_write((mapped == 0 && translated == phys && via_phys == 0xCAFEBABEu)
                   ? "\nVirtual memory mapping: OK\n"
                   : "\nVirtual memory mapping: FAILED\n");

    uint32_t old = paging_unmap_page(virt);
    if (old != 0)
        pmm_free_page(old);
}

// Sengaja memicu page fault: kernel berhenti dengan diagnostik CR2.
void cmd_pagefault(void)
{
    terminal_write("Touching unmapped 0xE0000000 (kernel will halt by design)...\n");
    *(volatile uint32_t *)0xE0000000u = 0x1234u;
}