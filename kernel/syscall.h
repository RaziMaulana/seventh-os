#ifndef SEVENTHOS_SYSCALL_H
#define SEVENTHOS_SYSCALL_H

#include "idt.h"

#define SYS_EXIT   1u
#define SYS_WRITE  2u
#define SYS_GETPID 3u
#define SYS_YIELD  4u

void syscall_handler(isr_frame_t *frame);

#endif