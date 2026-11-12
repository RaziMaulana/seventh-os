#ifndef SEVENTHOS_SHELL_UTIL_H
#define SEVENTHOS_SHELL_UTIL_H

#include <stdint.h>
#include "terminal.h"

static inline int parse_uint(const char **cursor, uint32_t *out)
{
    const char *s = *cursor;
    uint32_t value = 0;

    while (*s == ' ')
        s++;
    if (*s < '0' || *s > '9')
        return 0;
    while (*s >= '0' && *s <= '9')
        value = value * 10u + (uint32_t)(*s++ - '0');

    *cursor = s;
    *out = value;
    return 1;
}

static inline int word_is(const char *s, const char *word)
{
    uint32_t i = 0;

    while (word[i] != '\0')
    {
        if (s[i] != word[i])
            return 0;
        i++;
    }
    return s[i] == '\0' || s[i] == ' ';
}

static inline void print_check(const char *name, int ok, uint32_t *pass, uint32_t *fail)
{
    terminal_write("  ");
    terminal_set_color(ok ? COLOR_LIGHT_GREEN : COLOR_LIGHT_RED, COLOR_BLACK);
    terminal_write(ok ? "[PASS] " : "[FAIL] ");
    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    terminal_write(name);
    terminal_write("\n");
    if (ok)
        (*pass)++;
    else
        (*fail)++;
}

static inline uint32_t tenths(uint32_t sum, uint32_t n)
{
    return (n == 0) ? 0u : (sum * 20u / n + 1u) / 2u;
}

static inline void print_tenths(uint32_t value)
{
    terminal_write_dec(value / 10u);
    terminal_putchar('.');
    terminal_write_dec(value % 10u);
}

static inline void print_bar(uint32_t percent)
{
    if (percent > 100u)
        percent = 100u;

    uint32_t filled = percent * 20u / 100u;

    terminal_putchar('[');
    for (uint32_t i = 0; i < 20u; i++)
        terminal_putchar(i < filled ? '#' : '.');
    terminal_putchar(']');
}

#endif