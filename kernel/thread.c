#include <stddef.h>
#include <stdint.h>

#include "cpu.h"
#include "heap.h"
#include "kstring.h"
#include "scheduler.h"
#include "terminal.h"
#include "thread.h"
#include "timer.h"
#include "process.h"
#include "sync.h"

#define THREAD_STACK_SIZE 8192u
#define LOG_SIZE 32u
#define LOG_SHOW 12u

typedef struct
{
    uint32_t tick;
    uint32_t id;
    uint8_t from;
    uint8_t to;
} log_entry_t;

thread_t *g_current_thread;

static thread_t *list_head;
static thread_t *list_tail;
static thread_t *idle_thread_ptr;
static uint32_t next_id;
static log_entry_t event_log[LOG_SIZE];
static uint32_t log_count;

const char *thread_state_name(thread_state_t state)
{
    static const char *names[] = {"NEW", "READY", "RUNNING", "WAITING", "TERMINATED"};
    return ((unsigned)state < 5u) ? names[state] : "?";
}

static void set_state(thread_t *t, thread_state_t to)
{
    thread_state_t from = t->state;
    t->state = to;

    if ((from == THREAD_READY && to == THREAD_RUNNING) ||
        (from == THREAD_RUNNING && to == THREAD_READY))
        return;

    log_entry_t *e = &event_log[log_count % LOG_SIZE];
    e->tick = timer_get_ticks();
    e->id = t->id;
    e->from = (uint8_t)from;
    e->to = (uint8_t)to;
    log_count++;
}

static void list_append(thread_t *t)
{
    t->next = NULL;
    if (list_tail != NULL)
        list_tail->next = t;
    else
        list_head = t;
    list_tail = t;
}

static thread_t *find_locked(uint32_t id)
{
    for (thread_t *t = list_head; t != NULL; t = t->next)
        if (t->id == id)
            return t;
    return NULL;
}

static void thread_trampoline(void)
{
    __asm__ volatile("sti");
    g_current_thread->entry(g_current_thread->arg);
    thread_exit();
}

static void idle_entry(void *arg)
{
    (void)arg;
    for (;;)
        __asm__ volatile("sti; hlt");
}

thread_t *thread_create(const char *name, thread_entry_t entry, void *arg, uint32_t priority)
{
    thread_reap();

    thread_t *t = kcalloc(1, sizeof(thread_t));
    if (t == NULL)
        return NULL;

    uint8_t *stack = kmalloc(THREAD_STACK_SIZE);
    if (stack == NULL)
    {
        kfree(t);
        return NULL;
    }

    for (size_t i = 0; i < sizeof(t->name) - 1 && name[i] != '\0'; i++)
        t->name[i] = name[i];

    t->state = THREAD_NEW;
    t->priority = priority;
    t->entry = entry;
    t->arg = arg;
    t->stack_base = stack;
    t->stack_size = THREAD_STACK_SIZE;
    t->time_slice = THREAD_DEFAULT_SLICE;

    uint32_t *sp = (uint32_t *)(((uint32_t)(uintptr_t)stack + THREAD_STACK_SIZE) & ~0xFu);
    t->kstack_top = (uint32_t)(uintptr_t)sp; // <-- DITAMBAHKAN DI SINI
    *--sp = 0;
    *--sp = (uint32_t)(uintptr_t)thread_trampoline;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    t->esp = (uint32_t)(uintptr_t)sp;

    uint32_t flags = irq_save();
    t->id = next_id++;
    list_append(t);
    log_count++;
    log_count--;
    set_state(t, THREAD_NEW);
    irq_restore(flags);
    return t;
}

void thread_start(thread_t *t)
{
    uint32_t flags = irq_save();
    if (t->state == THREAD_NEW)
        t->arrival_tick = timer_get_ticks();
    set_state(t, THREAD_READY);
    irq_restore(flags);
}

void threads_initialize(void)
{
    thread_t *boot = kcalloc(1, sizeof(thread_t));
    const char *name = "shell";

    for (size_t i = 0; name[i] != '\0'; i++)
        boot->name[i] = name[i];
    boot->id = next_id++;
    boot->state = THREAD_RUNNING;
    boot->priority = 8;
    boot->time_slice = THREAD_DEFAULT_SLICE;
    boot->ticks_left = THREAD_DEFAULT_SLICE;
    list_append(boot);
    g_current_thread = boot;

    idle_thread_ptr = thread_create("idle", idle_entry, NULL, 0);
    idle_thread_ptr->is_idle = 1;
    thread_start(idle_thread_ptr);
}

// <-- FUNGSI PEMBANTU DITAMBAHKAN DI SINI
static void release_resources(thread_t *t)
{
    sync_thread_cleanup(t);
    process_thread_cleanup(t);
}

void thread_exit(void)
{
    (void)irq_save();
    release_resources(g_current_thread); // <-- DIPANGGIL DI SINI
    g_current_thread->finish_tick = timer_get_ticks();
    set_state(g_current_thread, THREAD_TERMINATED);
    schedule();
    for (;;)
        __asm__ volatile("hlt");
}

int thread_kill(uint32_t id)
{
    uint32_t flags = irq_save();
    thread_t *t = find_locked(id);

    if (t == NULL || t->is_idle || t->id == 0 || t->state == THREAD_TERMINATED)
    {
        irq_restore(flags);
        return -1;
    }
    if (t == g_current_thread)
        thread_exit();

    release_resources(t); // <-- DIPANGGIL DI SINI
    t->sleeping = 0;
    t->finish_tick = timer_get_ticks();
    set_state(t, THREAD_TERMINATED);
    irq_restore(flags);
    return 0;
}

void thread_yield(void)
{
    uint32_t flags = irq_save();
    schedule();
    irq_restore(flags);
}

void thread_sleep_ms(uint32_t ms)
{
    if (ms > 40000000u)
        ms = 40000000u;

    uint32_t wait = (ms * timer_get_frequency() + 999u) / 1000u;
    if (wait == 0)
        wait = 1;

    uint32_t flags = irq_save();
    g_current_thread->wake_tick = timer_get_ticks() + wait;
    g_current_thread->sleeping = 1;
    set_state(g_current_thread, THREAD_WAITING);
    schedule();
    irq_restore(flags);
}

void thread_block(void)
{
    uint32_t flags = irq_save();
    g_current_thread->sleeping = 0;
    set_state(g_current_thread, THREAD_WAITING);
    schedule();
    irq_restore(flags);
}

void thread_unblock(thread_t *t)
{
    uint32_t flags = irq_save();
    if (t->state == THREAD_WAITING && !t->sleeping)
        set_state(t, THREAD_READY);
    irq_restore(flags);
}

void thread_wake_sleepers(uint32_t now)
{
    for (thread_t *t = list_head; t != NULL; t = t->next)
    {
        if (t->state == THREAD_WAITING && t->sleeping &&
            (int32_t)(now - t->wake_tick) >= 0)
        {
            t->sleeping = 0;
            set_state(t, THREAD_READY);
        }
    }
}

int thread_set_priority(uint32_t id, uint32_t priority)
{
    uint32_t flags = irq_save();
    thread_t *t = find_locked(id);
    int rc = -1;

    if (t != NULL && t->state != THREAD_TERMINATED)
    {
        t->priority = priority;
        rc = 0;
    }
    irq_restore(flags);
    return rc;
}

void thread_reap(void)
{
    uint32_t flags = irq_save();
    thread_t *prev = NULL;
    thread_t *t = list_head;

    while (t != NULL)
    {
        thread_t *next = t->next;

        if (t->state == THREAD_TERMINATED && t != g_current_thread)
        {
            if (prev != NULL)
                prev->next = next;
            else
                list_head = next;
            if (t == list_tail)
                list_tail = prev;
            kfree(t->stack_base);
            kfree(t);
        }
        else
        {
            prev = t;
        }
        t = next;
    }
    irq_restore(flags);
}

thread_t *thread_first(void) { return list_head; }
thread_t *thread_idle(void) { return idle_thread_ptr; }

uint32_t thread_count(void)
{
    uint32_t n = 0;
    uint32_t flags = irq_save();

    for (thread_t *t = list_head; t != NULL; t = t->next)
        if (t->state != THREAD_TERMINATED)
            n++;
    irq_restore(flags);
    return n;
}

void thread_log_print(void)
{
    uint32_t flags = irq_save();
    uint32_t count = (log_count < LOG_SHOW) ? log_count : LOG_SHOW;
    uint32_t first = log_count - count;

    if (count == 0)
        terminal_write("  (no events yet)\n");

    for (uint32_t i = 0; i < count; i++)
    {
        const log_entry_t *e = &event_log[(first + i) % LOG_SIZE];
        terminal_write("  tick ");
        terminal_write_dec(e->tick);
        terminal_write("  thread ");
        terminal_write_dec(e->id);
        terminal_write(": ");
        terminal_write(thread_state_name((thread_state_t)e->from));
        terminal_write(" -> ");
        terminal_write(thread_state_name((thread_state_t)e->to));
        terminal_write("\n");
    }
    irq_restore(flags);
}