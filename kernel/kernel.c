#include <stddef.h>
#include <stdint.h>

#include "idt.h"
#include "keyboard.h"
#include "memory.h"
#include "shell.h"
#include "terminal.h"
#include "multiboot.h"
#include "pmm.h"

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

    switch (letter)
    {
    case 'S':
        return patterns[0][row];
    case 'E':
        return patterns[1][row];
    case 'V':
        return patterns[2][row];
    case 'N':
        return patterns[3][row];
    case 'T':
        return patterns[4][row];
    case 'H':
        return patterns[5][row];
    case 'O':
        return patterns[6][row];
    default:
        return "00000";
    }
}

static void terminal_write_banner(void)
{
    static const char word[] = "SEVENTHOS";

    terminal_write("\n");
    for (size_t row = 0; row < 5; row++)
    {
        terminal_write("             ");
        for (size_t character = 0; word[character] != '\0'; character++)
        {
            const char *pattern = letter_pattern(word[character], row);
            for (size_t column = 0; column < 5; column++)
                terminal_write(pattern[column] == '1' ? "\xdb" : " ");
            if (word[character + 1] != '\0')
                terminal_write(" ");
        }
        terminal_write("\n");
    }
}

void kernel_main(uint32_t multiboot_magic,
                 uint32_t multiboot_info_addr)
{
    terminal_initialize();

    if (multiboot_magic != 0x2BADB002 ||
        multiboot_info_addr == 0)
    {
        terminal_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
        terminal_write("Multiboot: invalid boot information\n");

        for (;;)
            __asm__ volatile("cli; hlt");
    }

    terminal_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
    terminal_write("Multiboot: boot information received\n");

    terminal_set_color(COLOR_LIGHT_BLUE, COLOR_BLACK);
    terminal_write_banner();

    terminal_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
    terminal_write("Kernel status: RUNNING\n");

    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    terminal_write("Architecture: x86 32-bit\n");
    terminal_write("Bootloader: GRUB Multiboot\n");
    terminal_write("Milestone 1: Kernel successfully loaded!\n");

    // Milestone 5: Parse Multiboot Memory Information.
    multiboot_parse(multiboot_magic, multiboot_info_addr);

    pmm_initialize(multiboot_info_addr);

    uintptr_t test_page = pmm_alloc_page();

    if (test_page != 0)
    {
        terminal_write("Page Allocation: SUCCESS\n");

        pmm_free_page(test_page);

        terminal_write("Page Deallocation: SUCCESS\n");
    }
    else
    {
        terminal_write("Page Allocation: FAILED\n");
    }

    terminal_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    terminal_write("\n=== PHYSICAL MEMORY MANAGER ===\n");

    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);

    terminal_write("Total Pages: ");
    terminal_write("Initialized\n");

    terminal_write("PMM Initialization: SUCCESS\n");

    // Initialize basic kernel heap.
    memory_initialize();

    terminal_write("Memory manager: ready (256 KiB kernel heap)\n");

    if (memory_allocate(16) != 0)
        terminal_write("Memory test: 16-byte allocation successful\n");
    else
        terminal_write("Memory test: allocation failed\n");

    // Initialize keyboard and interrupt handling.
    keyboard_initialize();
    idt_initialize();

    __asm__ volatile("sti");

    terminal_write("Interrupts: IDT and keyboard IRQ enabled\n\n");

    // Initialize shell.
    shell_initialize();

    for (;;)
    {
        shell_update();
        __asm__ volatile("hlt");
    }
}
