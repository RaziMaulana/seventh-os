#include <stddef.h>
#include <stdint.h>

#include "terminal.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25

static volatile uint16_t *const VGA_MEMORY = (volatile uint16_t *)0xB8000;
static size_t cursor_row;
static size_t cursor_column;
static uint8_t terminal_color = COLOR_LIGHT_GREY | (COLOR_BLACK << 4);

static uint16_t make_vga_entry(unsigned char character, uint8_t color)
{
    return (uint16_t)character | ((uint16_t)color << 8);
}

// Anggota 2: bersihkan semua sel VGA dan reset posisi kursor logis.
void terminal_clear(void)
{
    for (size_t row = 0; row < VGA_HEIGHT; row++)
        for (size_t column = 0; column < VGA_WIDTH; column++)
            VGA_MEMORY[row * VGA_WIDTH + column] = make_vga_entry(' ', terminal_color);
    cursor_row = 0;
    cursor_column = 0;
}

// Anggota 2: siapkan keadaan terminal; output awal dimulai di kiri atas.
void terminal_initialize(void)
{
    terminal_color = COLOR_LIGHT_GREY | (COLOR_BLACK << 4);
    terminal_clear();
}

// Anggota 2: ubah warna karakter dan latar untuk output berikutnya.
void terminal_set_color(uint8_t foreground, uint8_t background)
{
    terminal_color = (uint8_t)((foreground & 0x0Fu) | ((background & 0x0Fu) << 4));
}

// Anggota 2: gulir layar satu baris ketika output mencapai baris terakhir.
static void terminal_scroll(void)
{
    for (size_t row = 1; row < VGA_HEIGHT; row++)
        for (size_t column = 0; column < VGA_WIDTH; column++)
            VGA_MEMORY[(row - 1) * VGA_WIDTH + column] =
                VGA_MEMORY[row * VGA_WIDTH + column];
    for (size_t column = 0; column < VGA_WIDTH; column++)
        VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + column] = make_vga_entry(' ', terminal_color);
    cursor_row = VGA_HEIGHT - 1;
}

static void terminal_newline(void)
{
    cursor_column = 0;
    cursor_row++;
    if (cursor_row >= VGA_HEIGHT)
        terminal_scroll();
}

// Anggota 2: tampilkan satu karakter; tangani newline dan backspace.
void terminal_putchar(char character)
{
    if (character == '\n') {
        terminal_newline();
        return;
    }
    if (character == '\b') {
        if (cursor_column == 0) {
            if (cursor_row == 0)
                return;
            cursor_row--;
            cursor_column = VGA_WIDTH;
        }
        cursor_column--;
        VGA_MEMORY[cursor_row * VGA_WIDTH + cursor_column] = make_vga_entry(' ', terminal_color);
        return;
    }
    if (cursor_column >= VGA_WIDTH)
        terminal_newline();
    VGA_MEMORY[cursor_row * VGA_WIDTH + cursor_column] =
        make_vga_entry((unsigned char)character, terminal_color);
    cursor_column++;
}

// Anggota 2: fungsi output string dibangun dari operasi karakter terminal.
void terminal_write(const char *text)
{
    for (size_t i = 0; text[i] != '\0'; i++) {
        unsigned char first = (unsigned char)text[i];

        if (first == 0xE2 &&
            (unsigned char)text[i + 1] == 0x96 &&
            text[i + 2] != '\0') {
            unsigned char third = (unsigned char)text[i + 2];
            unsigned char cp437;

            switch (third) {
                case 0x80: cp437 = 223; break;
                case 0x84: cp437 = 220; break;
                case 0x88: cp437 = 219; break;
                case 0x8C: cp437 = 221; break;
                case 0x91: cp437 = 176; break;
                case 0x92: cp437 = 177; break;
                case 0x93: cp437 = 178; break;
                default:
                    if (third == 0x90)
                        cp437 = 222;
                    else
                        cp437 = '?';
                    break;
            }
            terminal_putchar((char)cp437);
            i += 2;
            continue;
        }

        terminal_putchar((char)first);
    }
}
