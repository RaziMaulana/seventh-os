#include <stddef.h>
#include <stdint.h>
#include "cmd_memory.h"
#include "cmd_sync.h"
#include "cmd_thread.h"
#include "cmd_user.h"
#include "keyboard.h"
#include "kstring.h"
#include "shell.h"
#include "shell_util.h"
#include "terminal.h"
#include "cmd_fs.h"
#include "cmd_inspect.h"
#include "cmd_exp.h"
#include "version.h"
#include "cmd_demo.h"

#define INPUT_BUFFER_SIZE 128

static char input_buffer[INPUT_BUFFER_SIZE];
static size_t input_length;

typedef struct
{
    const char *name;
    void (*run0)(void);             // perintah tanpa argumen
    void (*run1)(const char *args); // perintah dengan argumen (args bisa "")
} shell_cmd_t;

static void sh_help(void);
static void sh_clear(void) { terminal_clear(); }
static void sh_about(void) { terminal_write("SeventhOS is an educational 32-bit OS.\n"); }
static void sh_version(void) { terminal_write("SeventhOS v" SEVENTHOS_VERSION "\n"); }
static void sh_echo(const char *args)
{
    terminal_write(args);
    terminal_write("\n");
}

static const shell_cmd_t commands[] = {
    {"help", sh_help, NULL},
    {"clear", sh_clear, NULL},
    {"about", sh_about, NULL},
    {"version", sh_version, NULL},
    {"echo", NULL, sh_echo},
    /* Fase 2 */
    {"meminfo", cmd_meminfo, NULL},
    {"memmap", cmd_memmap, NULL},
    {"heapinfo", cmd_heapinfo, NULL},
    {"kmtest", cmd_kmtest, NULL},
    {"oomtest", cmd_oomtest, NULL},
    {"pmmtest", cmd_pmmtest, NULL},
    {"pagetest", cmd_pagetest, NULL},
    {"pagefault", cmd_pagefault, NULL},
    /* Fase 3 */
    {"timer", cmd_timer, NULL},
    {"delay", NULL, cmd_delay},
    {"ps", cmd_ps, NULL},
    {"threadlog", cmd_threadlog, NULL},
    {"spawn", NULL, cmd_spawn},
    {"kill", NULL, cmd_kill},
    {"prio", NULL, cmd_prio},
    {"sched", NULL, cmd_sched},
    {"reap", cmd_reap, NULL},
    /* Fase 4 */
    {"racetest", NULL, cmd_racetest},
    {"deadlock", NULL, cmd_deadlock},
    {"locks", cmd_locks, NULL},
    {"gdtinfo", cmd_gdtinfo, NULL},
    {"exec", NULL, cmd_exec},
    {"procs", cmd_procs, NULL},
    /* Fase 5: filesystem */
    {"pwd", cmd_pwd, NULL},
    {"cd", NULL, cmd_cd},
    {"ls", NULL, cmd_ls},
    {"mkdir", NULL, cmd_mkdir},
    {"touch", NULL, cmd_touch},
    {"cat", NULL, cmd_cat},
    {"write", NULL, cmd_write},
    {"append", NULL, cmd_append},
    {"rm", NULL, cmd_rm},
    {"rmdir", NULL, cmd_rmdir},
    {"stat", NULL, cmd_stat},
    {"df", cmd_df, NULL},
    {"fstest", cmd_fstest, NULL},
    {"fsfill", cmd_fsfill, NULL},
    {"sysinfo", cmd_sysinfo, NULL},
    {"top", cmd_top, NULL},
    {"irqinfo", cmd_irqinfo, NULL},
    {"diag", cmd_diag, NULL},
    {"schedsim", NULL, cmd_schedsim},
    {"schedlive", NULL, cmd_schedlive},
    {"fitexp", NULL, cmd_fitexp},
    {"demo", NULL, cmd_demo},
    {"selftest", NULL, cmd_selftest},
};

#define COMMAND_COUNT (sizeof(commands) / sizeof(commands[0]))

static void sh_help(void)
{
    uint32_t column = 0;
    terminal_write("Commands:\n");
    for (uint32_t i = 0; i < COMMAND_COUNT; i++)
    {
        uint32_t len = (uint32_t)strlen(commands[i].name);
        if (column + len + 1u > 78u)
        {
            terminal_write("\n");
            column = 0;
        }
        terminal_write(commands[i].name);
        terminal_putchar(' ');
        column += len + 1u;
    }
    terminal_write("\n");
}

static void shell_prompt(void)
{
    terminal_set_color(COLOR_YELLOW, COLOR_BLACK);
    terminal_write("seventhOS");
    terminal_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    terminal_write(":");
    terminal_write(cmd_fs_cwd());
    terminal_set_color(COLOR_YELLOW, COLOR_BLACK);
    terminal_write("> ");
    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
}

static void shell_write_character(char character)
{
    terminal_putchar(character);
}

static void shell_execute(const char *command)
{
    if (command[0] == '\0')
        return;

    for (uint32_t i = 0; i < COMMAND_COUNT; i++)
    {
        const shell_cmd_t *c = &commands[i];
        if (!word_is(command, c->name))
            continue;
        const char *args = command;
        while (*args != '\0' && *args != ' ')
            args++;
        while (*args == ' ')
            args++;
        if (c->run0 != NULL)
            c->run0();
        else
            c->run1(args);
        return;
    }

    terminal_write("Perintah tidak dikenal. Ketik 'help'.\n");
}

void shell_run_command(const char *command)
{
    shell_execute(command);
}

void shell_initialize(void)
{
    input_length = 0;
    input_buffer[0] = '\0';
    shell_prompt();
}

void shell_update(void)
{
    char character = keyboard_getchar();

    if (character == 0)
        return;

    if (character == '\b')
    {
        if (input_length > 0)
        {
            input_length--;
            input_buffer[input_length] = '\0';

            terminal_putchar('\b');
            terminal_putchar(' ');
            terminal_putchar('\b');
        }
        return;
    }

    if (character == '\n' || character == '\r')
    {
        terminal_write("\n");
        shell_execute(input_buffer);

        input_length = 0;
        input_buffer[0] = '\0';
        shell_prompt();
        return;
    }

    if (character >= 32 && character <= 126 &&
        input_length < INPUT_BUFFER_SIZE - 1)
    {
        input_buffer[input_length++] = character;
        input_buffer[input_length] = '\0';
        shell_write_character(character);
    }
}