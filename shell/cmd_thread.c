#include <stddef.h>

#include <stdint.h>

#include "cmd_thread.h"

#include "terminal.h"

#include "timer.h"

#include "cpu.h"

#include "demo_threads.h"

#include "scheduler.h"

#include "thread.h"

#include "shell_util.h"

#include "sync.h"

static void print_uptime(uint32_t ms)

{

    terminal_write_dec(ms / 1000u);

    terminal_putchar('.');

    if (ms % 1000u < 100u)

        terminal_putchar('0');

    if (ms % 1000u < 10u)

        terminal_putchar('0');

    terminal_write_dec(ms % 1000u);

    terminal_write(" s");
}

void cmd_timer(void)

{

    terminal_write("PIT channel 0, mode 3 (square wave)\n  frequency: ");

    terminal_write_dec(timer_get_frequency());

    terminal_write(" Hz\n  ticks    : ");

    terminal_write_dec(timer_get_ticks());

    terminal_write("\n  uptime   : ");

    print_uptime(timer_get_uptime_ms());

    terminal_write("\n");
}

void cmd_delay(const char *args)

{

    uint32_t ms;

    if (!parse_uint(&args, &ms) || ms == 0 || ms > 60000u)

    {

        terminal_write("usage: delay <1-60000 ms>\n");

        return;
    }

    uint32_t start = timer_get_uptime_ms();

    timer_delay_ms(ms);

    terminal_write("requested ");

    terminal_write_dec(ms);

    terminal_write(" ms, measured by PIT: ");

    terminal_write_dec(timer_get_uptime_ms() - start);

    terminal_write(" ms\n");
}

static void print_padded(const char *s, uint32_t width)

{

    uint32_t n = 0;

    while (s[n] != '\0')

        terminal_putchar(s[n++]);

    for (; n < width; n++)

        terminal_putchar(' ');
}

static void print_dec_padded(uint32_t value, uint32_t width)

{

    uint32_t digits = 1;

    for (uint32_t t = value; t >= 10u; t /= 10u)

        digits++;

    terminal_write_dec(value);

    for (; digits < width; digits++)

        terminal_putchar(' ');
}

void cmd_ps(void)

{

    terminal_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);

    terminal_write("ID  NAME        STATE       PRI CPU   SW   WAITS-ON\n");

    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);

    uint32_t flags = irq_save();

    for (thread_t *t = thread_first(); t != NULL; t = t->next)

    {

        print_dec_padded(t->id, 4);

        print_padded(t->name, 12);

        print_padded(thread_state_name(t->state), 12);

        print_dec_padded(t->priority, 4);

        print_dec_padded(t->cpu_ticks, 6);

        print_dec_padded(t->switches, 5);

        if (t->waiting_on != NULL)

            terminal_write(t->waiting_on->name);

        terminal_putchar('\n');
    }

    irq_restore(flags);
}

void cmd_threadlog(void)

{

    terminal_write("Thread state changes (READY<->RUNNING preemption not logged; see SW in ps):\n");

    thread_log_print();
}

void cmd_spawn(const char *args)

{

    static uint32_t next_column = 77;

    uint32_t priority = 5;

    thread_t *t = NULL;

    while (*args == ' ')

        args++;

    const char *kind = args;

    while (*args != '\0' && *args != ' ')

        args++;

    parse_uint(&args, &priority);

    uint32_t column = next_column;

    next_column = (next_column > 60u) ? next_column - 1u : 77u;

    if (word_is(kind, "cpu"))

        t = demo_create_cpu_bound(column, priority);

    else if (word_is(kind, "io"))

        t = demo_create_io_bound(column, priority);

    else if (word_is(kind, "once"))

        t = demo_create_finite(column, priority);

    else

    {

        terminal_write("usage: spawn <cpu|io|once> [priority]\n");

        return;
    }

    if (t == NULL)

    {

        terminal_write("spawn: out of memory\n");

        return;
    }

    terminal_write("thread ");

    terminal_write_dec(t->id);

    terminal_write(" '");

    terminal_write(t->name);

    terminal_write("' created: ");

    terminal_write(thread_state_name(t->state)); // NEW

    thread_start(t);

    terminal_write(" -> ");

    terminal_write(thread_state_name(t->state)); // READY

    terminal_write("\n");
}

void cmd_kill(const char *args)

{

    uint32_t id;

    if (!parse_uint(&args, &id))

    {

        terminal_write("usage: kill <thread id>\n");

        return;
    }

    terminal_write(thread_kill(id) == 0 ? "thread terminated\n"

                                        : "kill failed (not found, idle, shell, or already terminated)\n");
}

void cmd_prio(const char *args)

{

    uint32_t id, priority;

    if (!parse_uint(&args, &id) || !parse_uint(&args, &priority))

    {

        terminal_write("usage: prio <thread id> <priority>\n");

        return;
    }

    terminal_write(thread_set_priority(id, priority) == 0 ? "priority updated\n"

                                                          : "prio failed (thread not found)\n");
}

void cmd_sched(const char *args)

{

    while (*args == ' ')

        args++;

    if (word_is(args, "rr"))

        scheduler_set_policy(SCHED_ROUND_ROBIN);

    else if (word_is(args, "prio"))

        scheduler_set_policy(SCHED_PRIORITY);

    uint32_t hz = timer_get_frequency();

    terminal_write("policy        : ");

    terminal_write(scheduler_policy_name());

    terminal_write("\nquantum       : ");

    terminal_write_dec(THREAD_DEFAULT_SLICE);

    terminal_write(" ticks (");

    terminal_write_dec(THREAD_DEFAULT_SLICE * 1000u / hz);

    terminal_write(" ms)\ncontext switch: ");

    terminal_write_dec(scheduler_get_switches());

    terminal_write("\nthreads alive : ");

    terminal_write_dec(thread_count());

    terminal_write("\nusage: sched [rr|prio]\n");
}

void cmd_reap(void)

{

    thread_reap();

    terminal_write("terminated threads reaped\n");
}