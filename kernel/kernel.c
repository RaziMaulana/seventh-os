#include <stddef.h>

#include "idt.h"
#include "keyboard.h"
#include "memory.h"
#include "shell.h"
#include "terminal.h"

static const char *letter_pattern(char letter, size_t row)
{
    static const char *patterns[][5] = {
        {"11111", "10000", "11111", "00001", "11111"}, // S
        {"11111", "10000", "11111", "10000", "11111"}, // E
        {"10001", "10001", "10001", "10001", "01110"}, // V
        {"10001", "11001", "10101", "10011", "10001"}, // N
        {"11111", "00100", "00100", "00100", "00100"}, // T
        {"10001", "10001", "11111", "10001", "10001"}, // H
        {"11111", "10001", "10001", "10001", "11111"}  // O
    };

    switch (letter) {
        case 'S': return patterns[0][row];
        case 'E': return patterns[1][row];
        case 'V': return patterns[2][row];
        case 'N': return patterns[3][row];
        case 'T': return patterns[4][row];
        case 'H': return patterns[5][row];
        case 'O': return patterns[6][row];
        default: return "00000";
    }
}

static void terminal_write_banner(void)
{
    static const char word[] = "SEVENTHOS";

    terminal_write("\n");
    for (size_t row = 0; row < 5; row++) {
        terminal_write("             ");
        for (size_t character = 0; word[character] != '\0'; character++) {
            const char *pattern = letter_pattern(word[character], row);
            for (size_t column = 0; column < 5; column++)
                terminal_write(pattern[column] == '1' ? "\xdb" : " ");
            if (word[character + 1] != '\0')
                terminal_write(" ");
        }
        terminal_write("\n");
    }
}

void kernel_main(void)
{
    // Anggota 2: inisialisasi terminal VGA sebelum pesan kernel ditampilkan.
    terminal_initialize();
    terminal_set_color(COLOR_LIGHT_BLUE, COLOR_BLACK);
    terminal_write_banner();
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
