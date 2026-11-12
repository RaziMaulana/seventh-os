#include <stddef.h>
#include <stdint.h>

#include "idt.h"
#include "keyboard.h"
#include "heap.h"
#include "shell.h"
#include "terminal.h"
#include "multiboot.h"
#include "pmm.h"
#include "paging.h"
#include "timer.h"
#include "demo_threads.h"
#include "scheduler.h"
#include "thread.h"
#include "gdt.h"
#include "vfs.h"
#include "serial.h"
#include "selftest.h"

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

static void boot_status(const char *name, int ok)
{
    terminal_set_color(ok ? COLOR_LIGHT_GREEN : COLOR_LIGHT_RED, COLOR_BLACK);
    terminal_write(name);
    terminal_write(ok ? ": OK\n" : ": FAILED\n");
    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
}

static void kernel_panic(const char *message)
{
    terminal_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
    terminal_write("KERNEL PANIC: ");
    terminal_write(message);
    terminal_write("\n");
    for (;;)
        __asm__ volatile("cli; hlt");
}

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_addr)
{
    terminal_write_banner();
    terminal_initialize();
    serial_initialize(); // cermin keluaran ke COM1

    gdt_initialize(); // GDT Ring 0/3 + TSS: sebelum IDT
    boot_status("GDT + TSS (Ring 0/3)", 1);

    if (!multiboot_validate(multiboot_magic, multiboot_info_addr))
        kernel_panic("invalid Multiboot boot information");
    boot_status("Multiboot", 1);

    /* banner: biarkan kode banner Anda yang sudah ada di sini */

    pmm_initialize(multiboot_info_addr); // memori fisik
    boot_status("Physical Memory Manager", pmm_get_free_pages() > 0);

    idt_initialize(); // sebelum paging: page fault terlihat
    boot_status("IDT", 1);

    if (paging_initialize() != 0)
        kernel_panic("paging initialization failed");
    boot_status("Paging", paging_is_enabled());

    if (heap_initialize() != 0) // butuh PMM + paging
        kernel_panic("kernel heap initialization failed");
    boot_status("Kernel heap", 1);

    timer_initialize(TIMER_DEFAULT_HZ);
    boot_status("PIT timer (100 Hz)", 1);

    threads_initialize(); // adopsi shell (id 0) + idle (id 1)
    boot_status("Thread subsystem", thread_count() >= 2);

    boot_status("RAMDisk + SeventhFS (VFS)", vfs_init() == VFS_OK); // butuh heap + mutex

    scheduler_start();
    boot_status("Preemptive scheduler (Round Robin)", 1);

    keyboard_initialize();
    __asm__ volatile("sti");

    if (multiboot_cmdline_has("selftest"))
    { // mode pengujian otomatis (make test)
        terminal_write("Boot mode: selftest\n");
        uint32_t failed = selftest_run(1);
        selftest_exit(failed == 0);
        terminal_write("(no QEMU debug-exit device: continuing to the shell)\n");
    }

    terminal_write("Type 'help' for commands.\n\n");
    shell_initialize();
    for (;;)
    {
        shell_update();
        if (!keyboard_has_input())
            thread_sleep_ms(10);
    }
}
