#include <stdint.h>

#include "paging.h"
#include "process.h"
#include "syscall.h"
#include "terminal.h"
#include "thread.h"
#include "stats.h"

static uint32_t sys_write(uint32_t user_ptr, uint32_t length)
{
    if (length > 256u)
        length = 256u;

    // Kernel TIDAK boleh percaya pointer dari user. Validasi lewat page table:
    // seluruh rentang harus present + user-accessible.
    if (!paging_user_range_ok(user_ptr, length, 0)) {
        terminal_set_color(COLOR_YELLOW, COLOR_BLACK);
        terminal_write("[kernel] sys_write: bad user pointer ");
        terminal_write_hex(user_ptr);
        terminal_write(" rejected\n");
        terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
        return (uint32_t)-1;
    }

    const char *text = (const char *)(uintptr_t)user_ptr;
    for (uint32_t i = 0; i < length; i++)
        terminal_putchar(text[i]);
    return length;
}

void syscall_handler(isr_frame_t *f)
{
    stats_syscall(f->eax);

    if ((f->cs & 3u) != 3u) {            // syscall hanya untuk Ring 3
        f->eax = (uint32_t)-1;
        return;
    }

    switch (f->eax) {
    case SYS_EXIT:
        process_exit((int)f->ebx);       // tidak kembali
        break;
    case SYS_WRITE:
        f->eax = sys_write(f->ebx, f->ecx);
        break;
    case SYS_GETPID:
        f->eax = process_current_pid();
        break;
    case SYS_YIELD:
        thread_yield();
        f->eax = 0;
        break;
    default:
        f->eax = (uint32_t)-1;           // syscall tidak dikenal
        break;
    }
}