#include <stdint.h>

#include "cpu.h"
#include "timer.h"
#include "stats.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND 0x43
#define PIT_BASE_HZ 1193182u
#define PIC_MASTER_CMD 0x20
#define PIC_EOI 0x20

static volatile uint32_t ticks;
static uint32_t frequency = TIMER_DEFAULT_HZ;
static timer_callback_t callback;

void timer_initialize(uint32_t hz)
{
    if (hz < 19u)
        hz = 19u; // divisor 16-bit: minimum ~18.2 Hz
    if (hz > 1000u)
        hz = 1000u;

    frequency = hz;
    ticks = 0;

    uint32_t divisor = PIT_BASE_HZ / hz;

    outb(PIT_COMMAND, 0x36); // channel 0, lo/hi byte, mode 3, binary
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFFu));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFFu));
}

void timer_set_callback(timer_callback_t cb)
{
    callback = cb;
}

void timer_irq_handler(void)
{
    // EOI DULU: callback (scheduler) bisa berpindah thread dan baru kembali
    // beberapa tick kemudian. Tanpa EOI, IRQ0 berikutnya tidak akan datang.
    outb(PIC_MASTER_CMD, PIC_EOI);
    stats_irq(0);

    ticks++;
    if (callback != 0)
        callback();
}

uint32_t timer_get_ticks(void) { return ticks; }
uint32_t timer_get_frequency(void) { return frequency; }

uint32_t timer_get_uptime_ms(void)
{
    uint32_t t = ticks;
    // dipecah agar tidak overflow 32-bit pada uptime panjang
    return (t / frequency) * 1000u + ((t % frequency) * 1000u) / frequency;
}

void timer_delay_ms(uint32_t ms)
{
    uint32_t wait = (ms * frequency + 999u) / 1000u;
    uint32_t start = ticks;

    while ((uint32_t)(ticks - start) < wait)
        __asm__ volatile("hlt");
}