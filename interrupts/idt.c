#include <stdint.h>

#include "idt.h"
#include "terminal.h"
#include "paging.h"
#include "gdt.h"
#include "process.h"
#include "stats.h"

#define IDT_ENTRIES 256
#define PIC_MASTER_COMMAND 0x20
#define PIC_MASTER_DATA 0x21
#define PIC_SLAVE_COMMAND 0xA0
#define PIC_SLAVE_DATA 0xA1

extern void *isr_stub_table[32];
struct idt_entry
{
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attributes;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_pointer
{
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[IDT_ENTRIES];
extern void keyboard_irq_stub(void);
extern void keyboard_irq_handler(void);
extern void timer_irq_stub(void);
extern void syscall_stub(void);

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void idt_set_gate_attr(uint8_t vector, uintptr_t handler, uint8_t attributes)
{
    idt[vector].offset_low = (uint16_t)(handler & 0xFFFFu);
    idt[vector].selector = KERNEL_CODE_SEL;
    idt[vector].zero = 0;
    idt[vector].type_attributes = attributes;
    idt[vector].offset_high = (uint16_t)((handler >> 16) & 0xFFFFu);
}

static void idt_set_gate(uint8_t vector, uintptr_t handler)
{
    idt_set_gate_attr(vector, handler, 0x8E);
}

static void pic_initialize(void)
{
    outb(PIC_MASTER_COMMAND, 0x11);
    outb(PIC_SLAVE_COMMAND, 0x11);
    outb(PIC_MASTER_DATA, 0x20);
    outb(PIC_SLAVE_DATA, 0x28);
    outb(PIC_MASTER_DATA, 0x04);
    outb(PIC_SLAVE_DATA, 0x02);
    outb(PIC_MASTER_DATA, 0x01);
    outb(PIC_SLAVE_DATA, 0x01);
    outb(PIC_MASTER_DATA, 0xFC);
    outb(PIC_SLAVE_DATA, 0xFF);
}

void idt_initialize(void)
{
    struct idt_pointer pointer;
    for (uint16_t i = 0; i < IDT_ENTRIES; i++)
    {
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].zero = 0;
        idt[i].type_attributes = 0;
        idt[i].offset_high = 0;
    }

    for (uint8_t i = 0; i < 32; i++)
        idt_set_gate(i, (uintptr_t)isr_stub_table[i]);

    idt_set_gate(0x21, (uintptr_t)keyboard_irq_stub);
    idt_set_gate(0x20, (uintptr_t)timer_irq_stub);
    idt_set_gate_attr(0x80, (uintptr_t)syscall_stub, 0xEE); // DPL=3: boleh dipanggil dari Ring 3

    pointer.limit = (uint16_t)(sizeof(idt) - 1u);
    pointer.base = (uint32_t)(uintptr_t)idt;
    __asm__ volatile("lidt %0" : : "m"(pointer));
    pic_initialize();
}

static const char *exception_name(uint32_t vector)
{
    switch (vector)
    {
    case 0:
        return "divide error";
    case 6:
        return "invalid opcode";
    case 13:
        return "general protection fault";
    case 14:
        return "page fault";
    default:
        return "CPU exception";
    }
}

void exception_handler(isr_frame_t *f)
{
    stats_exception(f->vector);

    if (f->vector == 14)
        paging_page_fault_handler(f); // tidak kembali

    if ((f->cs & 3u) == 3u)                               
        process_handle_fault(f, exception_name(f->vector), 0); 

    static const char hex_digits[] = "0123456789ABCDEF";
    char message[] = "KERNEL EXCEPTION vector=0x00 error=0x00000000\n";
    for (uint8_t i = 0; i < 2; i++)
        message[26 + i] = hex_digits[(f->vector >> ((1u - i) * 4u)) & 0xFu];
    for (uint8_t i = 0; i < 8; i++)
        message[37 + i] = hex_digits[(f->error_code >> ((7u - i) * 4u)) & 0xFu];

    terminal_write(message);
    terminal_write(exception_name(f->vector));
    terminal_write(" at EIP=");
    terminal_write_hex(f->eip);
    terminal_write("\n");

    for (;;)
        __asm__ volatile("cli; hlt");
}