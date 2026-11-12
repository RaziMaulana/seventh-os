#ifndef SEVENTHOS_IDT_H
#define SEVENTHOS_IDT_H

#include <stdint.h>

// Tata letak stack yang dibentuk isr.asm (alamat rendah -> tinggi).
typedef struct
{
    uint32_t es, ds;                                       // ENTER_KERNEL
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax; // pusha
    uint32_t vector, error_code;                           // dari stub
    uint32_t eip, cs, eflags;                              // dari CPU
    uint32_t user_esp, user_ss;                            // hanya ada bila cs & 3 == 3
} isr_frame_t;

void idt_initialize(void);
void exception_handler(isr_frame_t *frame);

#endif