#include <stddef.h>
#include <stdint.h>

#include "memory.h"

#define HEAP_SIZE (256u * 1024u)
#define HEAP_ALIGNMENT 8u

// Anggota 4: heap awal terbatas; allocator ini bukan pengelola seluruh RAM.
static uint8_t kernel_heap[HEAP_SIZE] __attribute__((aligned(HEAP_ALIGNMENT)));
static size_t heap_used;

void memory_initialize(void)
{
    heap_used = 0;
}

// Anggota 4: alokasi bump beralignment 8 byte; NULL dikembalikan jika habis.
void *memory_allocate(size_t size)
{
    size_t aligned_size;

    if (size == 0 || size > (size_t)-1 - (HEAP_ALIGNMENT - 1u))
        return NULL;
    aligned_size = (size + (HEAP_ALIGNMENT - 1u)) & ~(HEAP_ALIGNMENT - 1u);
    if (aligned_size > HEAP_SIZE - heap_used)
        return NULL;

    void *allocation = &kernel_heap[heap_used];
    heap_used += aligned_size;
    return allocation;
}

size_t memory_available(void)
{
    return HEAP_SIZE - heap_used;
}
