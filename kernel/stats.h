#ifndef SEVENTHOS_STATS_H
#define SEVENTHOS_STATS_H

#include <stdint.h>

#define STATS_IRQ_LINES 16u
#define STATS_VECTORS 32u
#define STATS_SYSCALLS 8u

void stats_irq(uint32_t irq);
void stats_exception(uint32_t vector);
void stats_syscall(uint32_t number);

uint32_t stats_get_irq(uint32_t irq);
uint32_t stats_get_exception(uint32_t vector);
uint32_t stats_total_exceptions(void);
uint32_t stats_total_syscalls(void);

#endif