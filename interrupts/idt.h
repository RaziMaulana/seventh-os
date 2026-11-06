#ifndef SEVENTHOS_IDT_H
#define SEVENTHOS_IDT_H

#include <stdint.h>

// Anggota 4: siapkan IDT minimal dan aktifkan IRQ1 keyboard.
void idt_initialize(void);

// Dipanggil stub exception; tampilkan vektor lalu hentikan CPU dengan aman.
void exception_handler(uint32_t vector, uint32_t error_code);

#endif
