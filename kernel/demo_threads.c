#include <stdint.h>

#include "demo_threads.h"
#include "terminal.h"
#include "timer.h"

static const char frames[] = "|/-\\";

// CPU-bound: busy-wait, TIDAK PERNAH yield/sleep. Hanya preemption oleh timer
// yang bisa menghentikannya. Jika spinner lain dan shell tetap hidup, berarti
// preemption benar-benar bekerja (bukan simulasi output).
static void cpu_bound_task(void *arg)
{
    uint32_t column = (uint32_t)(uintptr_t)arg;
    uint32_t i = 0;
    uint32_t next = timer_get_ticks();

    for (;;)
    {
        if ((int32_t)(timer_get_ticks() - next) >= 0)
        {
            terminal_put_at(column, 0, frames[i++ & 3u], COLOR_LIGHT_GREEN);
            next += 10; // 100 ms
        }
    }
}

// I/O-bound: bekerja sebentar lalu tidur 250 ms -> sering berstatus WAITING.
static void io_bound_task(void *arg)
{
    uint32_t column = (uint32_t)(uintptr_t)arg;
    uint32_t i = 0;

    for (;;)
    {
        terminal_put_at(column, 0, frames[i++ & 3u], COLOR_LIGHT_CYAN);
        thread_sleep_ms(250);
    }
}

// Terbatas: berjalan ~2 detik lalu return -> TERMINATED (untuk melihat siklus hidup).
static void finite_task(void *arg)
{
    uint32_t column = (uint32_t)(uintptr_t)arg;

    for (uint32_t i = 0; i < 20; i++)
    {
        terminal_put_at(column, 0, frames[i & 3u], COLOR_YELLOW);
        thread_sleep_ms(100);
    }
    terminal_put_at(column, 0, ' ', COLOR_BLACK);
}

thread_t *demo_create_cpu_bound(uint32_t column, uint32_t priority)
{
    return thread_create("cpu-bound", cpu_bound_task, (void *)(uintptr_t)column, priority);
}

thread_t *demo_create_io_bound(uint32_t column, uint32_t priority)
{
    return thread_create("io-bound", io_bound_task, (void *)(uintptr_t)column, priority);
}

thread_t *demo_create_finite(uint32_t column, uint32_t priority)
{
    return thread_create("finite", finite_task, (void *)(uintptr_t)column, priority);
}