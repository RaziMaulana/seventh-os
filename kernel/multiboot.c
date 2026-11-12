
#include <stdint.h>

#include "multiboot.h"
#include "terminal.h"

static void print_hex64(uint64_t value)
{
    static const char digits[] = "0123456789ABCDEF";

    for (int i = 15; i >= 0; i--)
    {
        uint8_t digit = (value >> (i * 4)) & 0xF;
        terminal_putchar(digits[digit]);
    }
}

static void print_memory_type(uint32_t type)
{
    switch (type)
    {
    case 1:
        terminal_write("Available");
        break;

    case 2:
        terminal_write("Reserved");
        break;

    case 3:
        terminal_write("ACPI Reclaimable");
        break;

    case 4:
        terminal_write("ACPI NVS");
        break;

    case 5:
        terminal_write("Bad Memory");
        break;

    default:
        terminal_write("Unknown");
        break;
    }
}

static uint32_t saved_info_addr;

static char cmdline_copy[128];

int multiboot_validate(uint32_t magic, uint32_t info_addr)
{
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC || info_addr == 0)
        return 0;
    saved_info_addr = info_addr;

    // Salin SEKARANG, sebelum PMM/paging memakai halaman yang mungkin menimpa string ini.
    const multiboot_info_t *info = (const multiboot_info_t *)(uintptr_t)info_addr;

    if (info->flags & MULTIBOOT_INFO_CMDLINE)
    {
        const char *src = (const char *)(uintptr_t)info->cmdline;
        uint32_t i = 0;

        for (; i < sizeof(cmdline_copy) - 1u && src[i] != '\0'; i++)
            cmdline_copy[i] = src[i];
        cmdline_copy[i] = '\0';
    }
    return 1;
}

int multiboot_cmdline_has(const char *word)
{
    uint32_t w = 0;

    while (word[w] != '\0')
        w++;

    for (uint32_t i = 0; cmdline_copy[i] != '\0';)
    {
        while (cmdline_copy[i] == ' ')
            i++;

        uint32_t start = i;

        while (cmdline_copy[i] != '\0' && cmdline_copy[i] != ' ')
            i++;
        if (i - start == w)
        {
            uint32_t k = 0;

            while (k < w && cmdline_copy[start + k] == word[k])
                k++;
            if (k == w)
                return 1;
        }
    }
    return 0;
}

void multiboot_print_memory_map(void)
{
    multiboot_parse(MULTIBOOT_BOOTLOADER_MAGIC, saved_info_addr);
}

void multiboot_parse(uint32_t magic, uint32_t info_addr)
{
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC || info_addr == 0)
    {
        terminal_write("Multiboot: invalid information\n");
        return;
    }

    multiboot_info_t *info =
        (multiboot_info_t *)info_addr;

    terminal_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    terminal_write("\n=== MEMORY INFORMATION ===\n");

    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);

    if (info->flags & MULTIBOOT_INFO_MEMORY)
    {
        terminal_write("Lower Memory: ");
        terminal_write_dec(info->mem_lower);
        terminal_write(" KB\n");

        terminal_write("Upper Memory: ");
        terminal_write_dec(info->mem_upper);
        terminal_write(" KB\n");
    }
    else
    {
        terminal_write("Basic memory information unavailable\n");
    }

    if (!(info->flags & MULTIBOOT_INFO_MMAP))
    {
        terminal_write("Memory Map: unavailable\n");
        return;
    }

    if (info->mmap_addr == 0 || info->mmap_length == 0)
    {
        terminal_write("Memory Map: invalid\n");
        return;
    }

    terminal_write("\nMemory Map:\n");

    uint32_t offset = 0;
    uint32_t count = 0;

    while (offset < info->mmap_length)
    {
        if (info->mmap_length - offset < sizeof(uint32_t))
        {
            terminal_write("Memory Map: truncated entry\n");
            break;
        }

        multiboot_mmap_entry_t *entry =
            (multiboot_mmap_entry_t *)(info->mmap_addr + offset);

        uint32_t entry_size = entry->size;

        if (entry_size < 20 ||
            entry_size > info->mmap_length - offset - 4)
        {
            terminal_write("Memory Map: invalid entry\n");
            break;
        }

        terminal_write("Region ");
        terminal_write("Base: 0x");
        print_hex64(entry->addr);

        terminal_write(" Length: 0x");
        print_hex64(entry->len);

        terminal_write(" Type: ");
        print_memory_type(entry->type);

        terminal_putchar('\n');

        offset += entry_size + 4;
        count++;
    }

    terminal_write("\nTotal Regions: ");

    char digits[11];
    uint32_t index = 0;

    if (count == 0)
    {
        terminal_putchar('0');
    }
    else
    {
        while (count > 0)
        {
            digits[index++] = '0' + (count % 10);
            count /= 10;
        }

        while (index > 0)
            terminal_putchar(digits[--index]);
    }

    terminal_putchar('\n');

    terminal_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
    terminal_write("Multiboot Memory Parser: SUCCESS\n");
    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
}
