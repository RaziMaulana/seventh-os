#ifndef SEVENTHOS_HEAP_H
#define SEVENTHOS_HEAP_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint32_t heap_start;
    uint32_t heap_max;
    uint32_t total_bytes;    // ukuran heap yang sudah ter-map
    uint32_t used_bytes;     // payload blok terpakai
    uint32_t free_bytes;     // payload blok bebas
    uint32_t overhead_bytes; // total header
    uint32_t used_blocks;
    uint32_t free_blocks;
    uint32_t largest_free; // blok bebas terbesar (indikator fragmentasi)
    uint32_t alloc_count;
    uint32_t free_count;
    uint32_t failed_count; // jumlah kmalloc yang mengembalikan NULL
} heap_stats_t;

int heap_initialize(void); // 0 = sukses
void *kmalloc(size_t size);
void *kcalloc(size_t count, size_t size);
void kfree(void *ptr);

void heap_get_stats(heap_stats_t *stats);
void heap_dump(void);

#endif