#include <stddef.h>
#include <stdint.h>

#include "cpu.h"
#include "scheduler.h"
#include "thread.h"
#include "timer.h"
#include "gdt.h"

extern void context_switch(uint32_t *old_esp, uint32_t new_esp);

static sched_policy_t policy = SCHED_ROUND_ROBIN;
static int enabled;
static uint32_t total_switches;

// Pilih thread berikutnya, mulai dari thread SESUDAH yang sekarang (round robin).
// Thread idle hanya dipilih jika tidak ada thread lain yang READY.
static thread_t *pick_next(void)
{
    thread_t *start = (g_current_thread->next != NULL) ? g_current_thread->next
                                                       : thread_first();
    thread_t *t = start;
    thread_t *best = NULL;

    do
    {
        if (t->state == THREAD_READY && !t->is_idle)
        {
            if (policy == SCHED_ROUND_ROBIN)
                return t; // yang pertama READY setelah current
            if (best == NULL || t->priority > best->priority)
                best = t; // strict ">" : sesama prioritas tetap bergiliran
        }
        t = (t->next != NULL) ? t->next : thread_first();
    } while (t != start); // putaran penuh; current dikunjungi terakhir

    return (best != NULL) ? best : thread_idle();
}

void schedule(void)
{
    thread_t *prev = g_current_thread;

    if (prev->state == THREAD_RUNNING)
        prev->state = THREAD_READY; // preempt: kembali ke antrean

    thread_t *next = pick_next();
    next->state = THREAD_RUNNING;
    if (!next->ran_once)
    {
        next->ran_once = 1;
        next->first_run_tick = timer_get_ticks();
    }
    next->ticks_left = next->time_slice;

    if (next == prev)
        return;

    next->switches++;
    total_switches++;

    if (next->kstack_top != 0)
        tss_set_kernel_stack(next->kstack_top); // Ring 3 -> Ring 0 memakai stack thread ini

    g_current_thread = next;
    context_switch(&prev->esp, next->esp);
    // eksekusi lanjut di sini ketika prev dijadwalkan lagi
}

void scheduler_tick(void)
{
    if (!enabled)
        return;

    thread_wake_sleepers(timer_get_ticks());

    thread_t *cur = g_current_thread;
    cur->cpu_ticks++;
    if (cur->cpu_limit != 0 && cur->cpu_ticks >= cur->cpu_limit)
        thread_exit(); 
    if (cur->ticks_left > 0)
        cur->ticks_left--;

    if (policy == SCHED_PRIORITY || 
        cur->is_idle ||            
        cur->ticks_left == 0)     
        schedule();
}

void scheduler_start(void)
{
    timer_set_callback(scheduler_tick);
    enabled = 1;
}

void scheduler_set_policy(sched_policy_t p)
{
    uint32_t flags = irq_save();
    policy = p;
    irq_restore(flags);
}

sched_policy_t scheduler_get_policy(void) { return policy; }

const char *scheduler_policy_name(void)
{
    return (policy == SCHED_ROUND_ROBIN) ? "Round Robin" : "Priority (preemptive)";
}

uint32_t scheduler_get_switches(void) { return total_switches; }