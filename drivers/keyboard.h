// Membuat keyboard driver
// Membaca input dari keyboard
// Mengelola karakter yang diketik
#ifndef RAZIOS_KEYBOARD_H
#define RAZIOS_KEYBOARD_H

// Panggil sebelum interrupt diaktifkan.
void keyboard_initialize(void);

// Handler untuk IRQ1 (vektor 0x21).
void keyboard_irq_handler(void);

// Ambil karakter berikutnya; 0 jika buffer kosong.
char keyboard_getchar(void);

#endif
