
#include <stdint.h>
#include <stddef.h>

#include "pmm.h"
#include "multiboot.h"

#define PMM_MAX_PAGES (1024u * 1024u)
#define PMM_BITMAP_SIZE (PMM_MAX_PAGES / 8u)

extern uint8_t __kernel_start;
extern uint8_t __kernel_end;

static uint8_t bitmap[PMM_BITMAP_SIZE]
    __attribute__((aligned(4096)));

static uint8_t allocated_bitmap[PMM_BITMAP_SIZE]
    __attribute__((aligned(4096)));

static size_t total_pages;
static size_t free_pages;
static size_t used_pages;

static void allocated_set(size_t page)
{
    allocated_bitmap[page / 8u] |=
        (uint8_t)(1u << (page % 8u));
}

static void allocated_clear(size_t page)
{
    allocated_bitmap[page / 8u] &=
        (uint8_t)~(1u << (page % 8u));
}

static int allocated_test(size_t page)
{
    return (allocated_bitmap[page / 8u] &
            (1u << (page % 8u))) != 0;
}

static void bitmap_set(size_t page)
{
    bitmap[page / 8u] |= (uint8_t)(1u << (page % 8u));
}

static void bitmap_clear(size_t page)
{
    bitmap[page / 8u] &= (uint8_t)~(1u << (page % 8u));
}

static int bitmap_test(size_t page)
{
    return (bitmap[page / 8u] & (1u << (page % 8u))) != 0;
}

static void reserve_region(uint64_t start, uint64_t length)
{
    if (length == 0 || start >= 0x100000000ULL)
        return;

    uint64_t end = start + length;

    if (end < start || end > 0x100000000ULL)
        end = 0x100000000ULL;

    size_t first_page = (size_t)(start / PMM_PAGE_SIZE);
    size_t last_page = (size_t)((end - 1) / PMM_PAGE_SIZE);

    for (size_t page = first_page; page <= last_page; page++)
    {
        if (!bitmap_test(page))
        {
            bitmap_set(page);
            free_pages--;
            used_pages++;
        }
    }
}

static void release_region(uint64_t start, uint64_t length)
{
    if (length == 0 || start >= 0x100000000ULL)
        return;

    uint64_t end = start + length;

    if (end < start || end > 0x100000000ULL)
        end = 0x100000000ULL;

    uint64_t aligned_start =
        (start + PMM_PAGE_SIZE - 1u) &
        ~((uint64_t)PMM_PAGE_SIZE - 1u);

    uint64_t aligned_end =
        end & ~((uint64_t)PMM_PAGE_SIZE - 1u);

    if (aligned_start >= aligned_end)
        return;

    size_t first_page = (size_t)(aligned_start / PMM_PAGE_SIZE);
    size_t last_page = (size_t)(aligned_end / PMM_PAGE_SIZE);

    for (size_t page = first_page; page < last_page; page++)
    {
        if (bitmap_test(page))
        {
            bitmap_clear(page);
            free_pages++;
            used_pages--;
        }
    }
}

void pmm_initialize(uint32_t multiboot_info_addr)
{
    for (size_t i = 0; i < PMM_BITMAP_SIZE; i++)
        bitmap[i] = 0xFF;

    for (size_t i = 0; i < PMM_BITMAP_SIZE; i++)
        allocated_bitmap[i] = 0;

    total_pages = PMM_MAX_PAGES;
    free_pages = 0;
    used_pages = PMM_MAX_PAGES;

    multiboot_info_t *info =
        (multiboot_info_t *)multiboot_info_addr;

    if (!(info->flags & MULTIBOOT_INFO_MMAP) ||
        info->mmap_addr == 0 ||
        info->mmap_length == 0)
        return;

    uint32_t offset = 0;

    while (offset < info->mmap_length)
    {
        if (info->mmap_length - offset < sizeof(uint32_t))
            break;

        multiboot_mmap_entry_t *entry =
            (multiboot_mmap_entry_t *)(info->mmap_addr + offset);

        uint32_t entry_size = entry->size;

        if (entry_size < 20 ||
            entry_size > info->mmap_length - offset - 4)
            break;

        if (entry->type == 1)
        {
            release_region(entry->addr, entry->len);
        }

        offset += entry_size + 4;
    }

    // Reserve low memory.
    reserve_region(0, 0x100000);

    // Reserve the complete kernel image.
    reserve_region(
        (uintptr_t)&__kernel_start,
        (uintptr_t)&__kernel_end -
            (uintptr_t)&__kernel_start);

    // Reserve Multiboot information.
    reserve_region(multiboot_info_addr, sizeof(multiboot_info_t));

    // Reserve Multiboot memory map.
    reserve_region(info->mmap_addr, info->mmap_length);
}

uintptr_t pmm_alloc_page(void)
{
    for (size_t page = 256; page < PMM_MAX_PAGES; page++)
    {
        if (!bitmap_test(page))
        {
            bitmap_set(page);
            allocated_set(page);
            free_pages--;
            used_pages++;

            return (uintptr_t)(page * PMM_PAGE_SIZE);
        }
    }

    return 0;
}

void pmm_free_page(uintptr_t address)
{
    if (
        address < 0x100000 ||
        address % PMM_PAGE_SIZE != 0)
    {
        return;
    }

    size_t page = address / PMM_PAGE_SIZE;

    if (!allocated_test(page))
        return;

    allocated_clear(page);
    bitmap_clear(page);

    free_pages++;
    used_pages--;
}

size_t pmm_get_total_pages(void)
{
    return total_pages;
}

size_t pmm_get_free_pages(void)
{
    return free_pages;
}

size_t pmm_get_used_pages(void)
{
    return used_pages;
}
