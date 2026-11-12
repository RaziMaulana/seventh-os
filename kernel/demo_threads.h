#ifndef SEVENTHOS_DEMO_THREADS_H
#define SEVENTHOS_DEMO_THREADS_H

#include <stdint.h>
#include "thread.h"

// Semua mengembalikan thread berstatus NEW; panggil thread_start() untuk menjalankannya.
thread_t *demo_create_cpu_bound(uint32_t column, uint32_t priority);
thread_t *demo_create_io_bound(uint32_t column, uint32_t priority);
thread_t *demo_create_finite(uint32_t column, uint32_t priority);

#endif