#ifndef SEVENTHOS_GDT_H
#define SEVENTHOS_GDT_H

#include <stdint.h>

#define KERNEL_CODE_SEL 0x08
#define KERNEL_DATA_SEL 0x10
#define USER_CODE_SEL 0x1B // 0x18 | RPL 3
#define USER_DATA_SEL 0x23 // 0x20 | RPL 3
#define TSS_SEL 0x28

void gdt_initialize(void);
void tss_set_kernel_stack(uint32_t esp0);
void gdt_dump(void);

#endif