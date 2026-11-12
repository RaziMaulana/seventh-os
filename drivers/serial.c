#include <stdint.h>

#include "cpu.h"
#include "serial.h"

#define COM1 0x3F8u

static int serial_ready;

void serial_initialize(void)
{
    outb(COM1 + 1, 0x00); // matikan interrupt UART
    outb(COM1 + 3, 0x80); // DLAB = 1
    outb(COM1 + 0, 0x03); // divisor 3 -> 38400 baud
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); // 8 bit, tanpa paritas, 1 stop
    outb(COM1 + 2, 0xC7); // FIFO aktif
    outb(COM1 + 4, 0x0B); // RTS/DSR + OUT2
    serial_ready = 1;
}

static void put_raw(char c)
{
    for (uint32_t spin = 0; spin < 100000u && !(inb(COM1 + 5) & 0x20u); spin++)
    {
    }
    outb(COM1, (uint8_t)c);
}

void serial_write_char(char c)
{
    if (!serial_ready)
        return;
    if (c == '\n')
    {
        put_raw('\r');
        put_raw('\n');
    }
    else if (c == '\b')
    {
        put_raw('\b');
        put_raw(' ');
        put_raw('\b');
    }
    else
    {
        put_raw(c);
    }
}