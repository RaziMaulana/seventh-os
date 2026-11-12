#include "stats.h"

static volatile uint32_t irq_counts[STATS_IRQ_LINES];
static volatile uint32_t exc_counts[STATS_VECTORS];
static volatile uint32_t sys_counts[STATS_SYSCALLS];
static volatile uint32_t exc_total;
static volatile uint32_t sys_total;

void stats_irq(uint32_t irq)
{
    if (irq < STATS_IRQ_LINES)
        irq_counts[irq]++;
}

void stats_exception(uint32_t vector)
{
    if (vector < STATS_VECTORS)
        exc_counts[vector]++;
    exc_total++;
}

void stats_syscall(uint32_t number)
{
    if (number < STATS_SYSCALLS)
        sys_counts[number]++;
    sys_total++;
}

uint32_t stats_get_irq(uint32_t irq) { return (irq < STATS_IRQ_LINES) ? irq_counts[irq] : 0; }
uint32_t stats_get_exception(uint32_t vector) { return (vector < STATS_VECTORS) ? exc_counts[vector] : 0; }
uint32_t stats_total_exceptions(void) { return exc_total; }
uint32_t stats_total_syscalls(void) { return sys_total; }