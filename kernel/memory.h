#ifndef SEVENTHOS_MEMORY_H
#define SEVENTHOS_MEMORY_H

#include <stddef.h>

// Anggota 4: heap kernel statis untuk alokasi sederhana tanpa paging.
void memory_initialize(void);
void *memory_allocate(size_t size);
size_t memory_available(void);

#endif
