#include <stddef.h>
#include <stdint.h>

#include "cpu.h"
#include "gdt.h"
#include "kstring.h"
#include "paging.h"
#include "pmm.h"
#include "process.h"
#include "terminal.h"
#include "thread.h"

extern void enter_user_mode(uint32_t entry, uint32_t user_esp) __attribute__((noreturn));

extern const uint8_t user_hello_start[], user_hello_end[];
extern const uint8_t user_badptr_start[], user_badptr_end[];
extern const uint8_t user_kwrite_start[], user_kwrite_end[];
extern const uint8_t user_rowrite_start[], user_rowrite_end[];
extern const uint8_t user_priv_start[], user_priv_end[];
extern const uint8_t user_spin_start[], user_spin_end[];

typedef struct
{
    const char *name;
    const uint8_t *start;
    const uint8_t *end;
    int background;
    const char *description;
} user_program_t;

static const user_program_t programs[] = {
    {"hello", user_hello_start, user_hello_end, 0, "allowed access: syscall write, exit(pid)"},
    {"badptr", user_badptr_start, user_badptr_end, 0, "syscall with kernel pointer: rejected"},
    {"kwrite", user_kwrite_start, user_kwrite_end, 0, "writes kernel memory: killed (#PF)"},
    {"rowrite", user_rowrite_start, user_rowrite_end, 0, "writes own read-only code: killed (#PF)"},
    {"priv", user_priv_start, user_priv_end, 0, "executes HLT in Ring 3: killed (#GP)"},
    {"spin", user_spin_start, user_spin_end, 1, "infinite loop in Ring 3 (background)"},
};
#define PROGRAM_COUNT (sizeof(programs) / sizeof(programs[0]))

static process_t table[PROCESS_MAX];
static uint32_t next_pid = 1;

static int streq(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b)
    {
        a++;
        b++;
    }
    return *a == *b;
}

static const user_program_t *find_program(const char *name)
{
    for (uint32_t i = 0; i < PROGRAM_COUNT; i++)
        if (streq(programs[i].name, name))
            return &programs[i];
    return NULL;
}

static const char *state_name(proc_state_t s)
{
    switch (s)
    {
    case PROC_RUNNING:
        return "RUNNING";
    case PROC_EXITED:
        return "EXITED";
    case PROC_FAULTED:
        return "FAULTED";
    case PROC_KILLED:
        return "KILLED";
    default:
        return "UNUSED";
    }
}

/* ---------- memori proses ---------- */

static void unmap_all(process_t *p)
{
    if (p->code_mapped)
    {
        uint32_t phys = paging_unmap_page(p->code_base);
        if (phys != 0)
            pmm_free_page(phys);
        p->code_mapped = 0;
    }
    uint32_t stack_base = p->stack_top - USER_STACK_PAGES * PMM_PAGE_SIZE;

    for (uint32_t i = 0; i < p->stack_pages_mapped; i++)
    {
        uint32_t phys = paging_unmap_page(stack_base + i * PMM_PAGE_SIZE);
        if (phys != 0)
            pmm_free_page(phys);
    }
    p->stack_pages_mapped = 0;
}

static int map_pages(process_t *p, const user_program_t *prog, size_t size)
{
    uintptr_t code = pmm_alloc_page();

    if (code == 0)
        return -1;
    memset((void *)code, 0, PMM_PAGE_SIZE);
    memcpy((void *)code, prog->start, size); // salin blob lewat identity map

    // Kode: user + READ-ONLY (tanpa PAGE_WRITE).
    if (paging_map_page(p->code_base, (uint32_t)code, PAGE_PRESENT | PAGE_USER) != 0)
    {
        pmm_free_page(code);
        return -1;
    }
    p->code_mapped = 1;

    uint32_t stack_base = p->stack_top - USER_STACK_PAGES * PMM_PAGE_SIZE;

    for (uint32_t i = 0; i < USER_STACK_PAGES; i++)
    {
        uintptr_t page = pmm_alloc_page();

        if (page == 0)
            return -1;
        memset((void *)page, 0, PMM_PAGE_SIZE);
        if (paging_map_page(stack_base + i * PMM_PAGE_SIZE, (uint32_t)page,
                            PAGE_PRESENT | PAGE_WRITE | PAGE_USER) != 0)
        {
            pmm_free_page(page);
            return -1;
        }
        p->stack_pages_mapped++;
    }
    return 0;
}

/* ---------- siklus hidup proses ---------- */

// Berjalan di KERNEL MODE di atas kernel stack milik thread ini, lalu "melompat" ke Ring 3.
static void process_entry(void *arg)
{
    process_t *p = arg;

    tss_set_kernel_stack(g_current_thread->kstack_top);
    enter_user_mode(p->entry, p->stack_top);
}

int process_spawn(const char *program)
{
    const user_program_t *prog = find_program(program);

    if (prog == NULL)
        return -1;

    size_t size = (size_t)(prog->end - prog->start);
    if (size == 0 || size > PMM_PAGE_SIZE)
        return -2;

    uint32_t flags = irq_save();
    process_t *p = NULL;
    uint32_t slot = 0;

    for (uint32_t i = 0; i < PROCESS_MAX; i++)
    {
        if (table[i].state != PROC_RUNNING)
        {
            p = &table[i];
            slot = i;
            break;
        }
    }
    if (p == NULL)
    {
        irq_restore(flags);
        return -3;
    }

    memset(p, 0, sizeof(*p));
    p->pid = next_pid++;
    for (size_t i = 0; i < sizeof(p->name) - 1 && prog->name[i] != '\0'; i++)
        p->name[i] = prog->name[i];

    uint32_t slot_base = USER_REGION_BASE + slot * USER_SLOT_SIZE;
    p->code_base = slot_base;
    p->entry = slot_base;
    p->stack_top = slot_base + USER_STACK_OFFSET + USER_STACK_PAGES * PMM_PAGE_SIZE;

    if (map_pages(p, prog, size) != 0)
    {
        unmap_all(p);
        p->state = PROC_UNUSED;
        irq_restore(flags);
        return -4;
    }

    thread_t *t = thread_create(p->name, process_entry, p, 5);
    if (t == NULL)
    {
        unmap_all(p);
        p->state = PROC_UNUSED;
        irq_restore(flags);
        return -5;
    }

    t->process = p;
    p->thread = t;
    p->state = PROC_RUNNING;
    thread_start(t);
    irq_restore(flags);
    return (int)p->pid;
}

void process_exit(int code)
{
    process_t *p = g_current_thread->process;

    if (p != NULL)
    {
        p->state = PROC_EXITED;
        p->exit_code = code;
    }
    thread_exit(); // hook membebaskan halaman user
    for (;;)
    {
    }
}

// Jalur penanganan akses ilegal dari Ring 3: proses dihentikan, KERNEL TETAP HIDUP.
void process_handle_fault(isr_frame_t *f, const char *reason, uint32_t address)
{
    process_t *p = g_current_thread->process;

    terminal_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
    terminal_write("\n[kernel] PROTECTION FAULT: process ");
    terminal_write_dec(p != NULL ? p->pid : 0);
    terminal_write(" '");
    terminal_write(p != NULL ? p->name : "?");
    terminal_write("'\n  reason : ");
    terminal_write(reason);
    if (f->vector == 14)
    {
        terminal_write((f->error_code & 2u) ? " (write" : " (read");
        terminal_write((f->error_code & 1u) ? ", protection violation)" : ", page not present)");
        terminal_write("\n  address: ");
        terminal_write_hex(address);
    }
    terminal_write("\n  EIP    : ");
    terminal_write_hex(f->eip);
    terminal_write("\n  action : process terminated; kernel continues\n");
    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);

    if (p != NULL)
    {
        p->state = PROC_FAULTED;
        p->fault_vector = f->vector;
        p->fault_addr = address;
    }
    thread_exit();
    for (;;)
    {
    }
}

// Dipanggil thread.c (interrupt nonaktif) setiap thread berakhir atau dibunuh.
// Idempoten: aman dipanggil setelah process_exit/process_handle_fault.
void process_thread_cleanup(thread_t *t)
{
    process_t *p = t->process;

    if (p == NULL)
        return;
    if (p->state == PROC_RUNNING)
        p->state = PROC_KILLED;
    unmap_all(p); // halaman fisik kembali ke PMM
    p->thread = NULL;
    t->process = NULL;
}

/* ---------- informasi ---------- */

uint32_t process_current_pid(void)
{
    return (g_current_thread->process != NULL) ? g_current_thread->process->pid : 0;
}

static process_t *find_pid(uint32_t pid)
{
    for (uint32_t i = 0; i < PROCESS_MAX; i++)
        if (table[i].state != PROC_UNUSED && table[i].pid == pid)
            return &table[i];
    return NULL;
}

int process_is_running(uint32_t pid)
{
    process_t *p = find_pid(pid);
    return p != NULL && p->state == PROC_RUNNING;
}

int process_get_result(uint32_t pid, proc_state_t *state, int32_t *exit_code, uint32_t *fault_vector)
{
    process_t *p = find_pid(pid);

    if (p == NULL)
        return -1;
    *state = p->state;
    *exit_code = p->exit_code;
    *fault_vector = p->fault_vector;
    return 0;
}

int process_program_is_background(const char *program)
{
    const user_program_t *prog = find_program(program);
    return prog != NULL && prog->background;
}

void process_list_programs(void)
{
    terminal_write("usage: exec <program>\n");
    for (uint32_t i = 0; i < PROGRAM_COUNT; i++)
    {
        terminal_write("  ");
        terminal_write_pad(programs[i].name, 9);
        terminal_write(programs[i].description);
        terminal_write("\n");
    }
}

void process_print_result(uint32_t pid)
{
    process_t *p = find_pid(pid);

    if (p == NULL)
    {
        terminal_write("process not found\n");
        return;
    }
    terminal_write("process ");
    terminal_write_dec(p->pid);
    terminal_write(" '");
    terminal_write(p->name);
    terminal_write("': ");
    terminal_write(state_name(p->state));
    if (p->state == PROC_EXITED)
    {
        terminal_write(", exit code ");
        terminal_write_dec((uint32_t)p->exit_code);
    }
    else if (p->state == PROC_FAULTED)
    {
        terminal_write(", terminated by kernel (vector ");
        terminal_write_dec(p->fault_vector);
        terminal_write(")");
    }
    terminal_write("\n");
}

void process_print_table(void)
{
    terminal_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    terminal_write("PID NAME      STATE    THREAD CODE         STACK TOP\n");
    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);

    uint32_t flags = irq_save();
    for (uint32_t i = 0; i < PROCESS_MAX; i++)
    {
        process_t *p = &table[i];

        if (p->state == PROC_UNUSED)
            continue;
        terminal_write_dec_pad(p->pid, 4);
        terminal_write_pad(p->name, 10);
        terminal_write_pad(state_name(p->state), 9);
        if (p->thread != NULL)
            terminal_write_dec_pad(p->thread->id, 7);
        else
            terminal_write_pad("-", 7);
        terminal_write_hex(p->code_base);
        terminal_write("  ");
        terminal_write_hex(p->stack_top);
        terminal_write("\n");
    }
    irq_restore(flags);
}