#include <stddef.h>
#include <stdint.h>

#include "cmd_sync.h"
#include "shell_util.h"
#include "sync.h"
#include "sync_demo.h"
#include "terminal.h"

static const char *mode_name(race_mode_t m)
{
    return (m == RACE_NONE) ? "none (no lock)" : (m == RACE_MUTEX) ? "mutex"
                                                                   : "semaphore(1)";
}

static void run_and_print(race_mode_t mode, uint32_t iterations)
{
    race_result_t r;

    if (race_run(mode, iterations, &r) != 0)
    {
        terminal_write("racetest: failed (out of memory or timeout)\n");
        return;
    }

    terminal_write_pad(mode_name(mode), 15);
    terminal_write_dec_pad(r.expected, 9);
    terminal_write_dec_pad(r.actual, 9);
    terminal_write_dec_pad(r.lost, 9);
    terminal_write_dec_pad(r.blocked, 9);
    terminal_write_dec_pad(r.ticks, 7);

    if (r.lost == 0)
    {
        terminal_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
        terminal_write("CONSISTENT\n");
    }
    else
    {
        terminal_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
        terminal_write("RACE CONDITION (");
        terminal_write_dec(r.lost * 1000u / r.expected / 10u);
        terminal_write(".");
        terminal_write_dec(r.lost * 1000u / r.expected % 10u);
        terminal_write("% lost)\n");
    }
    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
}

void cmd_racetest(const char *args)
{
    uint32_t iterations = 500000u;
    uint32_t n;
    int all = 1;
    race_mode_t mode = RACE_NONE;

    while (*args == ' ')
        args++;

    if (word_is(args, "none"))
    {
        mode = RACE_NONE;
        all = 0;
        args += 4;
    }
    else if (word_is(args, "mutex"))
    {
        mode = RACE_MUTEX;
        all = 0;
        args += 5;
    }
    else if (word_is(args, "sem"))
    {
        mode = RACE_SEM;
        all = 0;
        args += 3;
    }

    if (parse_uint(&args, &n))
        iterations = n;

    if (iterations < 1000u || iterations > 1000000u)
    {
        terminal_write("usage: racetest [none|mutex|sem] [iterations 1000-1000000]\n");
        return;
    }

    terminal_write("2 threads x ");
    terminal_write_dec(iterations);
    terminal_write(" increments on one shared counter\n");
    terminal_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    terminal_write("MODE           EXPECTED ACTUAL   LOST     BLOCKED  TICKS  VERDICT\n");
    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);

    if (all)
    {
        run_and_print(RACE_NONE, iterations);
        run_and_print(RACE_MUTEX, iterations);
        run_and_print(RACE_SEM, iterations);
    }
    else
    {
        run_and_print(mode, iterations);
    }
}

void cmd_locks(void)
{
    sync_print_locks();
}

void cmd_deadlock(const char *args)
{
    while (*args == ' ')
        args++;

    if (*args == '\0') {
        deadlock_demo(0);
    } else if (word_is(args, "safe")) {
        deadlock_demo(1);
    } else if (word_is(args, "check")) {
        terminal_write(sync_detect_deadlock() == 0
                       ? "deadlock detector: no circular wait found\n"
                       : "recover with: kill <thread id>\n");
    } else {
        terminal_write("usage: deadlock [safe|check]\n");
    }
}