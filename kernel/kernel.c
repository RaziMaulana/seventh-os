#include "idt.h"
#include "keyboard.h"
#include "memory.h"
#include "shell.h"
#include "terminal.h"

void kernel_main(void)
{
    // Anggota 2: inisialisasi terminal VGA sebelum pesan kernel ditampilkan.
    terminal_initialize();
    terminal_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    terminal_write("             Welcome to SeventhOS          \n");

    // Anggota 2: tampilkan status kernel dengan warna yang dibedakan.
    terminal_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
    terminal_write("Kernel status: RUNNING\n");
    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    terminal_write("Architecture: x86 32-bit\n");
    terminal_write("Bootloader: GRUB Multiboot\n");
    terminal_write("Milestone 1: Kernel successfully loaded!\n");

    // Foto Anggota 4: siapkan heap sederhana dan tampilkan hasil uji alokasi.
    memory_initialize();
    terminal_write("Memory manager: ready (256 KiB kernel heap)\n");
    if (memory_allocate(16) != 0)
        terminal_write("Memory test: 16-byte allocation successful\n");
    else
        terminal_write("Memory test: allocation failed\n");

    // README Milestone 3: keyboard harus siap sebelum IDT dan CPU interrupt.
    keyboard_initialize();
    idt_initialize();
    __asm__ volatile ("sti");
    terminal_write("Interrupts: IDT and keyboard IRQ enabled\n\n");

    // Anggota 2: shell menjadi output interaktif setelah kernel siap.
    shell_initialize();
    for (;;) {
        shell_update();
        __asm__ volatile ("hlt");
    }
}
