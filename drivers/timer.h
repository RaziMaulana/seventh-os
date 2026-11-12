#ifndef SEVENTHOS_TIMER_H
#define SEVENTHOS_TIMER_H

#include <stdint.h>

#define TIMER_DEFAULT_HZ 100u

typedef void (*timer_callback_t)(void);

void timer_initialize(uint32_t hz);
void timer_irq_handler(void); // dipanggil dari timer_irq_stub
void timer_set_callback(timer_callback_t cb);

uint32_t timer_get_ticks(void);
uint32_t timer_get_frequency(void);
uint32_t timer_get_uptime_ms(void);
void timer_delay_ms(uint32_t ms); // menunggu dengan hlt; interrupt harus aktif

#endif