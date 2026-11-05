// Membuat command interpreter
// Mengembangkan perintah seperti help, clear, about, dan echo
#include <stddef.h>

#include "keyboard.h"
#include "shell.h"
#include "terminal.h"

#define INPUT_BUFFER_SIZE 128

// Menyimpan perintah yang sedang diketik.
static char input_buffer[INPUT_BUFFER_SIZE];
static size_t input_length;

// Tampilkan penanda input shell.
static void shell_prompt(void)
{
    terminal_set_color(COLOR_YELLOW, COLOR_BLACK);
    terminal_write("seventhOS> ");
    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
}

static void shell_write_character(char character)
{
    terminal_putchar(character);
}

// Bandingkan dua teks tanpa pustaka standar.
static int shell_string_equals(const char *left, const char *right)
{
    size_t index = 0;

    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index])
            return 0;
        index++;
    }

    return left[index] == right[index];
}

static int shell_starts_with(const char *text, const char *prefix)
{
    size_t index = 0;

    while (prefix[index] != '\0') {
        if (text[index] != prefix[index])
            return 0;
        index++;
    }

    return 1;
}

// Jalankan perintah yang dikenali shell.
static void shell_execute(const char *command)
{
    // OS dapat menjalankan perintah sederhana
    if (command[0] == '\0')
        return;

    if (shell_string_equals(command, "help")) {
        terminal_write("help\nclear\nabout\nversion\necho\n");
    } else if (shell_string_equals(command, "clear")) {
        terminal_clear();
    } else if (shell_string_equals(command, "about")) {
        terminal_write("SeventhOS is an educational 32-bit OS.\n");
    } else if (shell_string_equals(command, "version")) {
        terminal_write("SeventhOS v0.4\n");
    } else if (shell_string_equals(command, "echo")) {
        terminal_write("\n");
    } else if (shell_starts_with(command, "echo ")) {
        // Tampilkan teks setelah perintah echo.
        terminal_write(command + 5);
        terminal_write("\n");
    } else {
        terminal_write("Perintah tidak dikenal. Ketik 'help'.\n");
    }
}

void shell_initialize(void)
{
    // Kosongkan baris shell lalu tampilkan prompt awal.
    input_length = 0;
    input_buffer[0] = '\0';
    shell_prompt();
}

void shell_update(void)
{
    // Ambil satu karakter dari buffer keyboard.
    char character = keyboard_getchar();

    if (character == 0)
        return;

    if (character == '\b') {
        // Hapus karakter terakhir dari buffer.
        if (input_length > 0) {
            input_length--;
            input_buffer[input_length] = '\0';
            // Anggota 2: hapus karakter di layar bersama karakter buffer.
            terminal_putchar('\b');
        }
        return;
    }

    if (character == '\n' || character == '\r') {
        // Jalankan perintah, lalu siapkan baris baru.
        terminal_write("\n");
        shell_execute(input_buffer);

        input_length = 0;
        input_buffer[0] = '\0';
        shell_prompt();
        return;
    }

    if (character >= 32 && character <= 126 &&
        input_length < INPUT_BUFFER_SIZE - 1) {
        // Simpan dan tampilkan karakter yang diketik.
        input_buffer[input_length++] = character;
        input_buffer[input_length] = '\0';
        shell_write_character(character);
    }
}
