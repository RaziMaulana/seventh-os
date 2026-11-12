#include <stddef.h>
#include <stdint.h>

#include "cmd_user.h"
#include "gdt.h"
#include "process.h"
#include "terminal.h"
#include "thread.h"

void cmd_gdtinfo(void)
{
    gdt_dump();
}

void cmd_procs(void)
{
    process_print_table();
}

void cmd_exec(const char *args)
{
    while (*args == ' ')
        args++;

    if (*args == '\0')
    {
        process_list_programs();
        return;
    }

    int pid = process_spawn(args);

    if (pid == -1)
    {
        terminal_write("unknown program. Run 'exec' to list programs.\n");
        return;
    }
    if (pid < 0)
    {
        terminal_write("exec failed: no free process slot or out of memory\n");
        return;
    }

    terminal_write("process ");
    terminal_write_dec((uint32_t)pid);
    terminal_write(" started (Ring 3)\n");

    if (process_program_is_background(args))
    {
        terminal_write("running in background: see 'ps'/'procs', stop with: kill <thread id>\n");
        return;
    }

    uint32_t waited = 0;
    while (process_is_running((uint32_t)pid) && waited < 3000u)
    {
        thread_sleep_ms(20); // shell tidur; proses user memakai CPU
        waited += 20;
    }
    process_print_result((uint32_t)pid);
}