#include <stddef.h>
#include <stdint.h>

#define VGA_MEMORY ((volatile uint16_t*)0xB8000)
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define COLOR 0x0F

static size_t cursor_row = 0;
static size_t cursor_column = 0;

static uint16_t vga_entry(char character, uint8_t color) {
    return (uint16_t)character | ((uint16_t)color << 8);
}

void terminal_clear(void) {
    for (size_t row = 0; row < VGA_HEIGHT; row++) {
        for (size_t column = 0; column < VGA_WIDTH; column++) {
            VGA_MEMORY[row * VGA_WIDTH + column] = vga_entry(' ', COLOR);
        }
    }

    cursor_row = 0;
    cursor_column = 0;
}

void terminal_write(const char* text) {
    for (size_t i = 0; text[i] != '\0'; i++) {
        char character = text[i];

        if (character == '\n') {
            cursor_column = 0;
            cursor_row++;
            continue;
        }

        if (cursor_column >= VGA_WIDTH) {
            cursor_column = 0;
            cursor_row++;
        }

        if (cursor_row >= VGA_HEIGHT) {
            terminal_clear();
        }

        VGA_MEMORY[cursor_row * VGA_WIDTH + cursor_column] =
            vga_entry(character, COLOR);

        cursor_column++;
    }
}

void kernel_main(void) {
    terminal_clear();
    terminal_write("========================================\n");
    terminal_write("             Welcome to RaziOS          \n");
    terminal_write("========================================\n\n");
    terminal_write("Mini Operating System is booting...\n");
    terminal_write("Kernel status: RUNNING\n");
    terminal_write("Architecture: x86 32-bit\n");
    terminal_write("Bootloader: GRUB Multiboot\n\n");
    terminal_write("Milestone 1: Kernel successfully loaded!\n");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
