#include <stddef.h>
#include <stdint.h>

#include "heap.h"
#include "kstring.h"
#include "paging.h"
#include "pmm.h"
#include "cpu.h"
#include "terminal.h"

#define HEAP_START 0xD0000000u
#define HEAP_MAX_SIZE (16u * 1024u * 1024u)
#define HEAP_INITIAL (16u * 1024u)
#define HEAP_ALIGN 8u
#define HEAP_MAGIC 0xC0DE5EA7u

typedef struct heap_block
{
    uint32_t magic;
    uint32_t size; // ukuran payload
    uint32_t free;
    uint32_t reserved; // padding agar header 24 byte (kelipatan 8)
    struct heap_block *next;
    struct heap_block *prev;
} heap_block_t;

_Static_assert(sizeof(heap_block_t) % 8 == 0, "header heap harus kelipatan 8");

#define HDR ((uint32_t)sizeof(heap_block_t))

static heap_block_t *heap_head;
static heap_block_t *heap_tail;
static uint32_t heap_end; // ujung virtual heap saat ini
static uint32_t alloc_count, free_count, failed_count;

static uint32_t align_up(uint32_t v, uint32_t a)
{
    return (v + a - 1u) & ~(a - 1u);
}

// Tambah >= min_bytes ke heap: ambil halaman dari PMM, petakan ke heap_end.
static int heap_expand(uint32_t min_bytes)
{
    uint32_t bytes = align_up(min_bytes, PMM_PAGE_SIZE);

    if (bytes > HEAP_MAX_SIZE - (heap_end - HEAP_START))
        return -1; // batas virtual heap tercapai

    uint32_t mapped = 0;
    while (mapped < bytes)
    {
        uintptr_t phys = pmm_alloc_page();

        if (phys == 0 ||
            paging_map_page(heap_end + mapped, (uint32_t)phys,
                            PAGE_PRESENT | PAGE_WRITE) != 0)
        {
            if (phys != 0)
                pmm_free_page(phys);
            while (mapped > 0)
            { // rollback: kembalikan semua halaman
                mapped -= PMM_PAGE_SIZE;
                uint32_t old = paging_unmap_page(heap_end + mapped);
                if (old != 0)
                    pmm_free_page(old);
            }
            return -1; // RAM fisik habis
        }
        mapped += PMM_PAGE_SIZE;
    }

    if (heap_tail != NULL && heap_tail->free)
    {
        heap_tail->size += bytes; // perbesar blok bebas terakhir
    }
    else
    {
        heap_block_t *b = (heap_block_t *)(uintptr_t)heap_end;
        b->magic = HEAP_MAGIC;
        b->size = bytes - HDR;
        b->free = 1;
        b->reserved = 0;
        b->next = NULL;
        b->prev = heap_tail;
        if (heap_tail != NULL)
            heap_tail->next = b;
        else
            heap_head = b;
        heap_tail = b;
    }

    heap_end += bytes;
    return 0;
}

int heap_initialize(void)
{
    heap_head = NULL;
    heap_tail = NULL;
    heap_end = HEAP_START;
    alloc_count = free_count = failed_count = 0;
    return heap_expand(HEAP_INITIAL);
}

static void *kmalloc_locked(size_t size) 
{
    if (size == 0)
        return NULL;
    if (size > HEAP_MAX_SIZE)
    {
        failed_count++;
        return NULL;
    }

    uint32_t need = align_up((uint32_t)size, HEAP_ALIGN);

    for (;;)
    {
        for (heap_block_t *b = heap_head; b != NULL; b = b->next)
        {
            if (!b->free || b->size < need)
                continue; // first-fit

            if (b->size >= need + HDR + HEAP_ALIGN)
            { // split
                heap_block_t *rest =
                    (heap_block_t *)((uint8_t *)b + HDR + need);
                rest->magic = HEAP_MAGIC;
                rest->size = b->size - need - HDR;
                rest->free = 1;
                rest->reserved = 0;
                rest->prev = b;
                rest->next = b->next;
                if (b->next != NULL)
                    b->next->prev = rest;
                else
                    heap_tail = rest;
                b->next = rest;
                b->size = need;
            }

            b->free = 0;
            alloc_count++;
            return (void *)((uint8_t *)b + HDR);
        }

        // Tidak ada blok cocok: perbesar heap lalu cari lagi (pasti ketemu).
        if (heap_expand(need + HDR) != 0)
        {
            failed_count++;
            return NULL; // OOM: tanpa crash
        }
    }
}

void *kcalloc(size_t count, size_t size)
{
    if (count != 0 && size > HEAP_MAX_SIZE / count)
        return NULL; // cegah overflow perkalian

    void *p = kmalloc(count * size);
    if (p != NULL)
        memset(p, 0, count * size);
    return p;
}

static void kfree_locked(void *ptr) 
{
    if (ptr == NULL)
        return;

    uint32_t addr = (uint32_t)(uintptr_t)ptr;
    if (addr < HEAP_START + HDR || addr >= heap_end || (addr & (HEAP_ALIGN - 1u)))
    {
        terminal_write("kfree: invalid pointer\n");
        return;
    }

    heap_block_t *b = (heap_block_t *)(uintptr_t)(addr - HDR);
    if (b->magic != HEAP_MAGIC)
    {
        terminal_write("kfree: corrupted header\n");
        return;
    }
    if (b->free)
    {
        terminal_write("kfree: double free detected\n");
        return;
    }

    b->free = 1;
    free_count++;

    if (b->next != NULL && b->next->free)
    { // gabung dengan next
        heap_block_t *n = b->next;
        b->size += HDR + n->size;
        b->next = n->next;
        if (n->next != NULL)
            n->next->prev = b;
        else
            heap_tail = b;
        n->magic = 0;
    }

    if (b->prev != NULL && b->prev->free)
    { // gabung dengan prev
        heap_block_t *p = b->prev;
        p->size += HDR + b->size;
        p->next = b->next;
        if (b->next != NULL)
            b->next->prev = p;
        else
            heap_tail = p;
        b->magic = 0;
    }
}

void *kmalloc(size_t size)
{
    uint32_t flags = irq_save();
    void *p = kmalloc_locked(size);
    irq_restore(flags);
    return p;
}

void kfree(void *ptr)
{
    uint32_t flags = irq_save();
    kfree_locked(ptr);
    irq_restore(flags);
}

void heap_get_stats(heap_stats_t *s)
{
    memset(s, 0, sizeof(*s));
    s->heap_start = HEAP_START;
    s->heap_max = HEAP_MAX_SIZE;
    s->total_bytes = heap_end - HEAP_START;

    for (heap_block_t *b = heap_head; b != NULL; b = b->next)
    {
        s->overhead_bytes += HDR;
        if (b->free)
        {
            s->free_blocks++;
            s->free_bytes += b->size;
            if (b->size > s->largest_free)
                s->largest_free = b->size;
        }
        else
        {
            s->used_blocks++;
            s->used_bytes += b->size;
        }
    }
    s->alloc_count = alloc_count;
    s->free_count = free_count;
    s->failed_count = failed_count;
}

void heap_dump(void)
{
    uint32_t i = 0;

    terminal_write("Heap blocks (index addr size state):\n");
    for (heap_block_t *b = heap_head; b != NULL; b = b->next, i++)
    {
        if (i >= 14)
        {
            terminal_write("  ...\n");
            break;
        }
        terminal_write("  [");
        terminal_write_dec(i);
        terminal_write("] ");
        terminal_write_hex((uint32_t)(uintptr_t)b);
        terminal_write(" size=");
        terminal_write_dec(b->size);
        terminal_write(b->free ? " FREE\n" : " USED\n");
    }
}