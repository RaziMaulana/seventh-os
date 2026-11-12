#include <stddef.h>
#include <stdint.h>

#include "cmd_demo.h"
#include "cmd_exp.h"
#include "cmd_inspect.h"
#include "cmd_memory.h"
#include "cmd_sync.h"
#include "cmd_thread.h"
#include "cmd_user.h"
#include "cpu.h"
#include "demo_threads.h"
#include "keyboard.h"
#include "selftest.h"
#include "shell.h"
#include "shell_util.h"
#include "terminal.h"
#include "thread.h"
#include "cmd_fs.h"

typedef struct
{
    const char *title;
    void (*run)(void);
} demo_step_t;

static int same_name(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b)
    {
        a++;
        b++;
    }
    return *a == *b;
}

static void kill_by_name(const char *name)
{
    for (uint32_t attempt = 0; attempt < 8u; attempt++)
    {
        uint32_t id = 0xFFFFFFFFu;
        uint32_t flags = irq_save();

        for (thread_t *t = thread_first(); t != NULL; t = t->next)
        {
            if (t->state != THREAD_TERMINATED && same_name(t->name, name))
            {
                id = t->id;
                break;
            }
        }
        irq_restore(flags);
        if (id == 0xFFFFFFFFu)
            return;
        thread_kill(id);
    }
}

static void step_overview(void) { cmd_sysinfo(); }
static void step_memory(void)
{
    cmd_kmtest();
    terminal_putchar('\n');
    cmd_pagetest();
}
static void step_oom(void) { cmd_oomtest(); }

static void step_threads(void)
{
    thread_t *a = demo_create_cpu_bound(79, 5);
    thread_t *b = demo_create_io_bound(78, 5);

    if (a != NULL)
        thread_start(a);
    if (b != NULL)
        thread_start(b);
    terminal_write("Two threads with different tasks (spinners at top-right corner):\n");
    thread_sleep_ms(300);
    cmd_ps();
    cmd_top();
    if (a != NULL)
        thread_kill(a->id);
    if (b != NULL)
        thread_kill(b->id);
    thread_reap();
}

static void step_schedsim(void) { cmd_schedsim(""); }
static void step_schedlive(void) { cmd_schedlive(""); }
static void step_race(void) { cmd_racetest(""); }

static void step_deadlock(void)
{
    cmd_deadlock("");
    cmd_ps();
    terminal_write("-- recovery: killing victim dl-1 --\n");
    kill_by_name("dl-1");
    thread_sleep_ms(300);
    cmd_ps();
}

static void step_deadlock_safe(void) { cmd_deadlock("safe"); }
static void step_gdt(void) { cmd_gdtinfo(); }
static void step_user_ok(void)
{
    cmd_exec("hello");
    cmd_exec("badptr");
}
static void step_user_bad(void)
{
    cmd_exec("kwrite");
    cmd_exec("priv");
}
static void cmd_fstest_wrapper(void)
{
    uint32_t pass = 0, fail = 0;
    fstest_run(&pass, &fail);
}

static void step_fstest(void)
{
    cmd_fstest_wrapper();
}
static void step_fs_shell(void)
{
    shell_run_command("ls");
    shell_run_command("cat README.txt");
    shell_run_command("df");
}
static void step_fit(void) { cmd_fitexp("classic"); }
static void step_diag(void) { cmd_diag(); }

static const demo_step_t steps[] = {
    {"System overview", step_overview},
    {"Physical memory, paging, kernel heap", step_memory},
    {"Out-of-memory handling", step_oom},
    {"Threads and preemptive scheduling", step_threads},
    {"CPU scheduling algorithms (simulation)", step_schedsim},
    {"Round Robin on the real scheduler", step_schedlive},
    {"Race condition vs mutex/semaphore", step_race},
    {"Deadlock: detect and recover", step_deadlock},
    {"Deadlock prevention: lock ordering", step_deadlock_safe},
    {"GDT, TSS and privilege levels", step_gdt},
    {"User mode: allowed access", step_user_ok},
    {"User mode: illegal access is blocked", step_user_bad},
    {"Filesystem CRUD test", step_fstest},
    {"Filesystem from the shell", step_fs_shell},
    {"Memory allocation: First/Best/Worst Fit", step_fit},
    {"Kernel self-diagnostics", step_diag},
};
#define STEP_COUNT ((uint32_t)(sizeof(steps) / sizeof(steps[0])))

static void print_header(uint32_t index)
{
    terminal_set_color(COLOR_YELLOW, COLOR_BLACK);
    terminal_write("== Step ");
    terminal_write_dec(index + 1u);
    terminal_write("/");
    terminal_write_dec(STEP_COUNT);
    terminal_write(": ");
    terminal_write(steps[index].title);
    terminal_write(" ==\n");
    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
}

static int wait_key(void) // 1 = lanjut, 0 = berhenti (tekan q)
{
    char c;

    terminal_set_color(COLOR_YELLOW, COLOR_BLACK);
    terminal_write("\n[ any key = next step, q = stop ]");
    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    while ((c = keyboard_getchar()) == 0)
        thread_sleep_ms(20); // shell tidur; thread lain bebas berjalan
    return c != 'q';
}

void cmd_demo(const char *args)
{
    uint32_t only = 0;

    parse_uint(&args, &only); // "demo 8" menjalankan langkah 8 saja
    if (only != 0 && only <= STEP_COUNT)
    {
        print_header(only - 1u);
        steps[only - 1u].run();
        return;
    }

    for (uint32_t i = 0; i < STEP_COUNT; i++)
    {
        terminal_clear();
        print_header(i);
        steps[i].run();
        if (!wait_key())
            break;
    }
    terminal_write("\ndemo finished\n");
}

void cmd_selftest(const char *args)
{
    while (*args == ' ')
        args++;
    selftest_run(word_is(args, "-v"));
}