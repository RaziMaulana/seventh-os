#include <stddef.h>
#include <stdint.h>

#include "cpu.h"
#include "scheduler.h"
#include "sync.h"
#include "sync_demo.h"
#include "terminal.h"
#include "thread.h"
#include "timer.h"

#define RACE_WINDOW 30u // memperlebar jarak antara LOAD dan STORE

static volatile race_mode_t race_mode;
static volatile uint32_t race_iterations;
static volatile uint32_t shared_counter;
static volatile uint32_t workers_done;
static mutex_t counter_mutex;
static semaphore_t counter_sem;
static mutex_t lock_a, lock_b;
static volatile uint32_t dl_done;
static volatile int dl_safe;
static uint32_t dl_id1 = 0xFFFFFFFFu, dl_id2 = 0xFFFFFFFFu;

// Critical section SAMA untuk ketiga mode; yang berbeda hanya kunci di sekelilingnya.
static void race_worker(void *arg)
{
    (void)arg;

    for (uint32_t i = 0; i < race_iterations; i++)
    {
        if (race_mode == RACE_MUTEX)
            mutex_lock(&counter_mutex);
        else if (race_mode == RACE_SEM)
            sem_wait(&counter_sem);

        uint32_t tmp = shared_counter; // LOAD
        for (volatile uint32_t k = 0; k < RACE_WINDOW; k++)
        {
        } // jendela race
        shared_counter = tmp + 1; // STORE

        if (race_mode == RACE_MUTEX)
            mutex_unlock(&counter_mutex);
        else if (race_mode == RACE_SEM)
            sem_post(&counter_sem);
    }

    uint32_t flags = irq_save();
    workers_done++;
    irq_restore(flags);
}

int race_run(race_mode_t mode, uint32_t iterations, race_result_t *r)
{
    mutex_init(&counter_mutex, "counter", 10);
    sem_init(&counter_sem, "counter-sem", 1); // binary semaphore

    shared_counter = 0;
    workers_done = 0;
    race_mode = mode;
    race_iterations = iterations;

    uint32_t t0 = timer_get_ticks();
    uint32_t s0 = scheduler_get_switches();

    thread_t *a = thread_create("race-1", race_worker, NULL, 5);
    thread_t *b = thread_create("race-2", race_worker, NULL, 5);

    if (a == NULL || b == NULL)
    {
        if (a != NULL)
            thread_kill(a->id);
        if (b != NULL)
            thread_kill(b->id);
        return -1;
    }
    thread_start(a);
    thread_start(b);

    uint32_t waited = 0;
    while (workers_done < 2)
    {
        thread_sleep_ms(20); // shell tidur; worker bebas memakai CPU
        waited += 20;
        if (waited > 60000u)
            return -2;
    }

    r->expected = 2u * iterations;
    r->actual = shared_counter;
    r->lost = r->expected - r->actual;
    r->blocked = (mode == RACE_MUTEX) ? counter_mutex.contentions
                 : (mode == RACE_SEM) ? counter_sem.blocks
                                      : 0;
    r->ticks = timer_get_ticks() - t0;
    r->switches = scheduler_get_switches() - s0;
    return 0;
}

static void dl_finish(void)
{
    uint32_t flags = irq_save();
    dl_done++;
    irq_restore(flags);
}

// Thread 1: urutan A -> B (urutan "benar").
static void dl_thread_1(void *arg)
{
    (void)arg;
    mutex_lock(&lock_a);
    thread_sleep_ms(100); // beri waktu thread 2 mengunci mutex pertamanya
    mutex_lock(&lock_b);
    mutex_unlock(&lock_b);
    mutex_unlock(&lock_a);
    dl_finish();
}

// Thread 2: tidak aman = B -> A (terbalik). Aman = lock_pair (diurutkan menurut rank).
static void dl_thread_2(void *arg)
{
    (void)arg;
    if (dl_safe)
    {
        mutex_lock_pair(&lock_b, &lock_a); // internal: A dulu, baru B
        mutex_unlock_pair(&lock_b, &lock_a);
    }
    else
    {
        mutex_lock(&lock_b);
        thread_sleep_ms(100);
        mutex_lock(&lock_a);
        mutex_unlock(&lock_a);
        mutex_unlock(&lock_b);
    }
    dl_finish();
}

static int thread_alive(uint32_t id)
{
    int alive = 0;
    uint32_t flags = irq_save();

    for (thread_t *t = thread_first(); t != NULL; t = t->next)
        if (t->id == id && t->state != THREAD_TERMINATED)
            alive = 1;
    irq_restore(flags);
    return alive;
}

int deadlock_demo(int safe)
{
    if (thread_alive(dl_id1) || thread_alive(dl_id2))
    {
        terminal_write("previous deadlock demo threads are still alive: kill them first (see ps)\n");
        return -1;
    }

    mutex_init(&lock_a, "lock-A", 1);
    mutex_init(&lock_b, "lock-B", 2);
    sync_reset_order_violations();
    dl_done = 0;
    dl_safe = safe;

    thread_t *t1 = thread_create("dl-1", dl_thread_1, NULL, 5);
    thread_t *t2 = thread_create("dl-2", dl_thread_2, NULL, 5);

    if (t1 == NULL || t2 == NULL)
    {
        if (t1 != NULL)
            thread_kill(t1->id);
        if (t2 != NULL)
            thread_kill(t2->id);
        terminal_write("deadlock demo: out of memory\n");
        return -1;
    }
    dl_id1 = t1->id;
    dl_id2 = t2->id;

    terminal_write(safe ? "SAFE variant: thread 2 uses lock ordering (lock_pair)\n"
                        : "UNSAFE variant: thread 1 locks A->B, thread 2 locks B->A\n");
    thread_start(t1);
    thread_start(t2);

    if (safe)
    {
        uint32_t waited = 0;

        while (dl_done < 2 && waited < 3000u)
        {
            thread_sleep_ms(20);
            waited += 20;
        }
        if (dl_done == 2)
        {
            terminal_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
            terminal_write("Both threads completed. Circular wait prevented by lock ordering.\n");
            terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
        }
        else
        {
            terminal_write("timeout: unexpected, check 'ps' and 'locks'\n");
        }
        sync_print_locks();
    }
    else
    {
        thread_sleep_ms(600); // cukup untuk kedua thread saling mengunci
        if (sync_detect_deadlock() == 0)
            terminal_write("no deadlock detected (timing?), try again\n");
        else
        {
            terminal_write("Both threads are stuck in WAITING (see 'ps', 'locks').\n");
            terminal_write("Recover by killing one victim: kill <thread id>\n");
        }
    }
    return 0;
}