#ifndef SEVENTHOS_TERMINAL_H
#define SEVENTHOS_TERMINAL_H

#include <stdint.h>

// Anggota 2: warna foreground VGA yang didukung terminal.
enum terminal_color {
    COLOR_BLACK = 0,
    COLOR_BLUE = 1,
    COLOR_GREEN = 2,
    COLOR_CYAN = 3,
    COLOR_RED = 4,
    COLOR_MAGENTA = 5,
    COLOR_BROWN = 6,
    COLOR_LIGHT_GREY = 7,
    COLOR_DARK_GREY = 8,
    COLOR_LIGHT_BLUE = 9,
    COLOR_LIGHT_GREEN = 10,
    COLOR_LIGHT_CYAN = 11,
    COLOR_LIGHT_RED = 12,
    COLOR_LIGHT_MAGENTA = 13,
    COLOR_YELLOW = 14,
    COLOR_WHITE = 15
};

void terminal_initialize(void);
void terminal_clear(void);
void terminal_set_color(uint8_t foreground, uint8_t background);
void terminal_putchar(char character);
void terminal_write(const char *text);
void terminal_write_dec(uint32_t value);
void terminal_write_hex(uint32_t value); 
void terminal_put_at(uint32_t column, uint32_t row, char character, uint8_t foreground);
void terminal_write_pad(const char *text, uint32_t width);
void terminal_write_dec_pad(uint32_t value, uint32_t width);

#endif
