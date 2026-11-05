#include <stdint.h>

#include "idt.h"
#include "terminal.h"

#define IDT_ENTRIES 256
#define PIC_MASTER_COMMAND 0x20
#define PIC_MASTER_DATA 0x21
#define PIC_SLAVE_COMMAND 0xA0
#define PIC_SLAVE_DATA 0xA1

extern void *isr_stub_table[32];
struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attributes;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_pointer {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[IDT_ENTRIES];
extern void keyboard_irq_stub(void);
extern void keyboard_irq_handler(void);

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void idt_set_gate(uint8_t vector, uintptr_t handler)
{
    idt[vector].offset_low = (uint16_t)(handler & 0xFFFFu);
    // Selector code segment aktif GRUB pada kernel ini (CS=0x10).
    idt[vector].selector = 0x10;
    idt[vector].zero = 0;
    idt[vector].type_attributes = 0x8E;
    idt[vector].offset_high = (uint16_t)((handler >> 16) & 0xFFFFu);
}

// Anggota 4: remap PIC ke 0x20/0x28 dan hanya buka IRQ keyboard pada master.
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
    outb(PIC_MASTER_DATA, 0xFD); // IRQ1 terbuka; IRQ0 dan IRQ2 dimask.
    outb(PIC_SLAVE_DATA, 0xFF);
}

void idt_initialize(void)
{
    struct idt_pointer pointer;
    for (uint16_t i = 0; i < IDT_ENTRIES; i++) {
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].zero = 0;
        idt[i].type_attributes = 0;
        idt[i].offset_high = 0;
    }

    // Anggota 4: pasang diagnostic handler untuk 32 exception CPU pertama.
    for (uint8_t i = 0; i < 32; i++)
        idt_set_gate(i, (uintptr_t)isr_stub_table[i]);

    // Anggota 4: vektor 0x21 menghubungkan IRQ1 PIC dengan driver keyboard.
    idt_set_gate(0x21, (uintptr_t)keyboard_irq_stub);
    pointer.limit = (uint16_t)(sizeof(idt) - 1u);
    pointer.base = (uint32_t)(uintptr_t)idt;
    __asm__ volatile ("lidt %0" : : "m"(pointer));
    pic_initialize();
}

// Anggota 4: cetak nomor vektor/error code agar fault terlihat di layar QEMU.
void exception_handler(uint32_t vector, uint32_t error_code)
{
    static const char hex_digits[] = "0123456789ABCDEF";
    char message[] = "KERNEL EXCEPTION vector=0x00 error=0x00000000\n";
    for (uint8_t i = 0; i < 2; i++)
        message[26 + i] = hex_digits[(vector >> ((1u - i) * 4u)) & 0xFu];
    for (uint8_t i = 0; i < 8; i++)
        message[37 + i] = hex_digits[(error_code >> ((7u - i) * 4u)) & 0xFu];
    terminal_write(message);
    for (;;)
        __asm__ volatile ("cli; hlt");
}
