// Membuat keyboard driver
// Membaca input dari keyboard
// Mengelola karakter yang diketik
#include <stdint.h>

#include "keyboard.h"
#include "stats.h"

#define KEYBOARD_DATA_PORT 0x60
#define PIC_MASTER_COMMAND_PORT 0x20
#define PIC_EOI 0x20
#define KEYBOARD_BUFFER_SIZE 128

static const char scancode_map[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0A] = '9', [0x0B] = '0', [0x0C] = '-', [0x0D] = '=',
    [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1A] = '[', [0x1B] = ']',
    [0x1C] = '\n', [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd',
    [0x21] = 'f', [0x22] = 'g', [0x23] = 'h', [0x24] = 'j',
    [0x25] = 'k', [0x26] = 'l', [0x27] = ';', [0x28] = '\'',
    [0x29] = '`', [0x2B] = '\\', [0x2C] = 'z', [0x2D] = 'x',
    [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b', [0x31] = 'n',
    [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '/',
    [0x39] = ' '
};

static const char shifted_scancode_map[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
    [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
    [0x0A] = '(', [0x0B] = ')', [0x0C] = '_', [0x0D] = '+',
    [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
    [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
    [0x18] = 'O', [0x19] = 'P', [0x1A] = '{', [0x1B] = '}',
    [0x1C] = '\n', [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D',
    [0x21] = 'F', [0x22] = 'G', [0x23] = 'H', [0x24] = 'J',
    [0x25] = 'K', [0x26] = 'L', [0x27] = ':', [0x28] = '"',
    [0x29] = '~', [0x2B] = '|', [0x2C] = 'Z', [0x2D] = 'X',
    [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B', [0x31] = 'N',
    [0x32] = 'M', [0x33] = '<', [0x34] = '>', [0x35] = '?',
    [0x39] = ' '
};

static char input_buffer[KEYBOARD_BUFFER_SIZE];
static volatile uint16_t input_head;
static volatile uint16_t input_tail;
static volatile uint8_t left_shift_down;
static volatile uint8_t right_shift_down;
static volatile uint8_t caps_lock_on;
static volatile uint8_t extended_scancode;

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;

    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void input_buffer_push(char character)
{
    uint16_t next_head = (uint16_t)((input_head + 1u) % KEYBOARD_BUFFER_SIZE);

    // Abaikan karakter baru jika buffer penuh.
    if (next_head == input_tail)
        return;

    input_buffer[input_head] = character;
    input_head = next_head;
}

// Pengguna dapat mengetik pada terminal
static void handle_scancode(uint8_t scancode)
{
    uint8_t released = (uint8_t)(scancode & 0x80u);
    uint8_t key = (uint8_t)(scancode & 0x7Fu);

    if (key == 0x2A) {
        left_shift_down = (uint8_t)!released;
        return;
    }

    if (key == 0x36) {
        right_shift_down = (uint8_t)!released;
        return;
    }

    if (released)
        return;

    if (key == 0x3A) {
        caps_lock_on = (uint8_t)!caps_lock_on;
        return;
    }

    {
        uint8_t shift_down = (uint8_t)(left_shift_down || right_shift_down);
        char character = shift_down
            ? shifted_scancode_map[key]
            : scancode_map[key];

        if (character >= 'a' && character <= 'z' && (caps_lock_on != shift_down))
            character = (char)(character - 'a' + 'A');
        else if (character >= 'A' && character <= 'Z' && (caps_lock_on == shift_down))
            character = (char)(character - 'A' + 'a');

        if (character != '\0')
            input_buffer_push(character);
    }
}

void keyboard_initialize(void)
{
    input_head = 0;
    input_tail = 0;
    left_shift_down = 0;
    right_shift_down = 0;
    caps_lock_on = 0;
    extended_scancode = 0;
}

void keyboard_irq_handler(void)
{
    stats_irq(1);
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

    if (scancode == 0xE0) {
        extended_scancode = 1;
    } else if (extended_scancode) {
        // Abaikan tombol tambahan, seperti tombol panah.
        extended_scancode = 0;
    } else {
        handle_scancode(scancode);
    }

    // Kirim EOI IRQ1; ISR tidak perlu mengirim EOI lagi.
    outb(PIC_MASTER_COMMAND_PORT, PIC_EOI);
}

char keyboard_getchar(void)
{
    char character;

    // OS dapat membaca input keyboard
    if (input_head == input_tail)
        return 0;

    character = input_buffer[input_tail];
    input_tail = (uint16_t)((input_tail + 1u) % KEYBOARD_BUFFER_SIZE);
    return character;
}

int keyboard_has_input(void)
{
    return input_head != input_tail;
}
