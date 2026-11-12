
#ifndef SEVENTHOS_MULTIBOOT_H
#define SEVENTHOS_MULTIBOOT_H

#include <stdint.h>

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002
#define MULTIBOOT_INFO_MEMORY (1u << 0)
#define MULTIBOOT_INFO_MMAP (1u << 6)
#define MULTIBOOT_INFO_CMDLINE (1u << 2)

typedef struct __attribute__((packed))
{
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
} multiboot_info_t;

typedef struct __attribute__((packed))
{
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} multiboot_mmap_entry_t;

void multiboot_parse(uint32_t magic, uint32_t info_addr);
int multiboot_cmdline_has(const char *word);
int multiboot_validate(uint32_t magic, uint32_t info_addr);
void multiboot_print_memory_map(void);

#endif
