#include <stddef.h>
#include <stdint.h>

#include "cpu.h"
#include "sync.h"
#include "terminal.h"
#include "thread.h"

#define MAX_REGISTERED 16u
#define MAX_LOCKDEP_MSGS 3u
#define MAX_CYCLE 8u

static mutex_t *registry[MAX_REGISTERED];
static uint32_t registry_count;
static uint32_t order_violations;

/* ---------- antrean tunggu (FIFO) ---------- */

static void wq_push(wait_queue_t *q, thread_t *t)
{
    t->wait_next = NULL;
    if (q->tail != NULL)
        q->tail->wait_next = t;
    else
        q->head = t;
    q->tail = t;
}

static thread_t *wq_pop(wait_queue_t *q)
{
    thread_t *t = q->head;

    if (t != NULL)
    {
        q->head = t->wait_next;
        if (q->head == NULL)
            q->tail = NULL;
        t->wait_next = NULL;
    }
    return t;
}

static void wq_remove(wait_queue_t *q, thread_t *t)
{
    thread_t *prev = NULL;

    for (thread_t *c = q->head; c != NULL; prev = c, c = c->wait_next)
    {
        if (c == t)
        {
            if (prev != NULL)
                prev->wait_next = c->wait_next;
            else
                q->head = c->wait_next;
            if (q->tail == c)
                q->tail = prev;
            c->wait_next = NULL;
            return;
        }
    }
}

/* ---------- pencatatan mutex yang dipegang tiap thread ---------- */

static int held_add(thread_t *t, mutex_t *m)
{
    if (t->held_count >= THREAD_MAX_HELD_LOCKS)
        return -1;
    t->held_locks[t->held_count++] = m; // Ubah t->held menjadi t->held_locks
    return 0;
}

static void held_remove(thread_t *t, mutex_t *m)
{
    for (uint8_t i = 0; i < t->held_count; i++)
    {
        if (t->held_locks[i] == m) // Ubah t->held menjadi t->held_locks
        {
            t->held_locks[i] = t->held_locks[--t->held_count]; // Ubah t->held menjadi t->held_locks
            t->held_locks[t->held_count] = NULL;
            break;
        }
    }
}

// Lockdep sederhana: aturan "rank harus naik". Pelanggaran dilaporkan (bukan
// diblokir) agar demo deadlock tetap bisa memperlihatkan akibatnya.
static void check_order(thread_t *t, mutex_t *m)
{
    for (uint8_t i = 0; i < t->held_count; i++)
    {
        mutex_t *h = t->held_locks[i];

        if (h->rank >= m->rank)
        {
            order_violations++;
            if (order_violations <= MAX_LOCKDEP_MSGS)
            {
                terminal_set_color(COLOR_YELLOW, COLOR_BLACK);
                terminal_write("\n[lockdep] ORDER VIOLATION: thread ");
                terminal_write_dec(t->id);
                terminal_write(" locks '");
                terminal_write(m->name);
                terminal_write("' (rank ");
                terminal_write_dec(m->rank);
                terminal_write(") while holding '");
                terminal_write(h->name);
                terminal_write("' (rank ");
                terminal_write_dec(h->rank);
                terminal_write(")\n");
                terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
            }
            return;
        }
    }
}

/* ---------- mutex ---------- */

void mutex_init(mutex_t *m, const char *name, uint32_t rank)
{
    m->name = name;
    m->rank = rank;
    m->locked = 0;
    m->owner = NULL;
    m->waiters.head = NULL;
    m->waiters.tail = NULL;
    m->acquisitions = 0;
    m->contentions = 0;

    uint32_t flags = irq_save();
    int known = 0;

    for (uint32_t i = 0; i < registry_count; i++)
        if (registry[i] == m)
            known = 1;
    if (!known && registry_count < MAX_REGISTERED)
        registry[registry_count++] = m;
    irq_restore(flags);
}

static void grant(mutex_t *m, thread_t *t)
{
    m->locked = 1;
    m->owner = t;
    m->acquisitions++;
    held_add(t, m);
}

// Pemanggil sudah mengeluarkan mutex dari daftar 'held' pemilik lama.
// Serah-terima langsung ke penunggu pertama (tanpa celah "bebas").
static void release_locked(mutex_t *m)
{
    thread_t *next = wq_pop(&m->waiters);

    if (next != NULL)
    {
        next->wait_queue = NULL;
        next->waiting_on = NULL;
        grant(m, next);
        thread_unblock(next); // WAITING -> READY
    }
    else
    {
        m->locked = 0;
        m->owner = NULL;
    }
}

void mutex_lock(mutex_t *m)
{
    uint32_t flags = irq_save();
    thread_t *cur = g_current_thread;

    check_order(cur, m);

    if (!m->locked)
    {
        grant(m, cur);
    }
    else
    {
        m->contentions++;
        wq_push(&m->waiters, cur);
        cur->wait_queue = &m->waiters;
        cur->waiting_on = m;
        thread_block(); // kembali ke sini setelah mutex diserahkan kepada kita
    }
    irq_restore(flags);
}

int mutex_trylock(mutex_t *m)
{
    uint32_t flags = irq_save();
    int rc = -1;

    if (!m->locked)
    {
        grant(m, g_current_thread);
        rc = 0;
    }
    irq_restore(flags);
    return rc;
}

void mutex_unlock(mutex_t *m)
{
    uint32_t flags = irq_save();

    if (!m->locked || m->owner != g_current_thread)
    {
        terminal_write("[sync] unlock of '");
        terminal_write(m->name);
        terminal_write("' by a thread that does not own it\n");
    }
    else
    {
        held_remove(g_current_thread, m);
        release_locked(m);
    }
    irq_restore(flags);
}

// Lock ordering: dua mutex selalu dikunci dari rank kecil ke besar, apa pun
// urutan argumen. Dua thread yang memakai fungsi ini tidak mungkin saling
// menunggu melingkar (tidak ada circular wait).
void mutex_lock_pair(mutex_t *a, mutex_t *b)
{
    if (a->rank > b->rank || (a->rank == b->rank && a > b))
    {
        mutex_t *t = a;
        a = b;
        b = t;
    }
    mutex_lock(a);
    if (b != a)
        mutex_lock(b);
}

void mutex_unlock_pair(mutex_t *a, mutex_t *b)
{
    if (a->rank < b->rank || (a->rank == b->rank && a < b))
    {
        mutex_t *t = a;
        a = b;
        b = t; // lepas yang rank-nya lebih besar dulu
    }
    mutex_unlock(a);
    if (b != a)
        mutex_unlock(b);
}

/* ---------- semaphore (counting) ---------- */

void sem_init(semaphore_t *s, const char *name, int32_t initial)
{
    s->name = name;
    s->count = initial;
    s->waiters.head = NULL;
    s->waiters.tail = NULL;
    s->waits = 0;
    s->blocks = 0;
}

void sem_wait(semaphore_t *s)
{
    uint32_t flags = irq_save();

    s->waits++;
    if (s->count > 0)
    {
        s->count--;
    }
    else
    {
        s->blocks++;
        wq_push(&s->waiters, g_current_thread);
        g_current_thread->wait_queue = &s->waiters;
        thread_block(); // sem_post menyerahkan "token" langsung
    }
    irq_restore(flags);
}

void sem_post(semaphore_t *s)
{
    uint32_t flags = irq_save();
    thread_t *t = wq_pop(&s->waiters);

    if (t != NULL)
    {
        t->wait_queue = NULL;
        thread_unblock(t);
    }
    else
    {
        s->count++;
    }
    irq_restore(flags);
}

/* ---------- pembersihan saat thread berakhir / dibunuh ---------- */

// Tanpa ini, membunuh thread yang memegang mutex membuat mutex terkunci selamanya,
// dan thread TERMINATED yang masih ada di antrean akan menerima serah-terima.
void sync_thread_cleanup(thread_t *t)
{
    uint32_t flags = irq_save();

    if (t->wait_queue != NULL)
    {
        wq_remove(t->wait_queue, t);
        t->wait_queue = NULL;
        t->waiting_on = NULL;
    }
    while (t->held_count > 0)
    {
        mutex_t *m = t->held_locks[--t->held_count];
        mutex_unlock(m);

        if (m->owner == t)
            release_locked(m);
    }
    irq_restore(flags);
}

/* ---------- detektor deadlock (graf wait-for) ---------- */

// Mengikuti rantai: thread -> mutex yang ditunggu -> pemilik mutex -> ...
// Siklus yang kembali ke 'start' berarti circular wait.
static uint32_t find_cycle(thread_t *start, thread_t *path[], uint32_t max)
{
    thread_t *cur = start;
    uint32_t n = 0;

    while (cur != NULL && cur->state == THREAD_WAITING &&
           cur->waiting_on != NULL && n < max)
    {
        path[n++] = cur;
        cur = cur->waiting_on->owner;
        if (cur == start)
            return n;
    }
    return 0;
}

uint32_t sync_detect_deadlock(void)
{
    thread_t *path[MAX_CYCLE];
    uint32_t cycles = 0;
    uint32_t flags = irq_save();

    for (thread_t *t = thread_first(); t != NULL; t = t->next)
    {
        uint32_t n = find_cycle(t, path, MAX_CYCLE);

        if (n == 0)
            continue;

        uint32_t min_id = path[0]->id;
        for (uint32_t i = 1; i < n; i++)
            if (path[i]->id < min_id)
                min_id = path[i]->id;
        if (t->id != min_id)
            continue; // tiap siklus dilaporkan sekali

        cycles++;
        terminal_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
        terminal_write("DEADLOCK: circular wait among ");
        terminal_write_dec(n);
        terminal_write(" thread(s)\n");
        terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);

        for (uint32_t i = 0; i < n; i++)
        {
            mutex_t *m = path[i]->waiting_on;

            terminal_write("  thread ");
            terminal_write_dec(path[i]->id);
            terminal_write(" '");
            terminal_write(path[i]->name);
            terminal_write("' waits for '");
            terminal_write(m->name);
            terminal_write("' held by thread ");
            terminal_write_dec(m->owner->id);
            terminal_write(" '");
            terminal_write(m->owner->name);
            terminal_write("'\n");
        }
    }
    irq_restore(flags);
    return cycles;
}

void sync_print_locks(void)
{
    terminal_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    terminal_write("LOCK        RANK STATE   OWNER WAITERS ACQ     CONTENDED\n");
    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);

    uint32_t flags = irq_save();

    for (uint32_t i = 0; i < registry_count; i++)
    {
        mutex_t *m = registry[i];
        uint32_t waiters = 0;

        for (thread_t *w = m->waiters.head; w != NULL; w = w->wait_next)
            waiters++;

        terminal_write_pad(m->name, 12);
        terminal_write_dec_pad(m->rank, 5);
        terminal_write_pad(m->locked ? "locked" : "free", 8);
        if (m->owner != NULL)
            terminal_write_dec_pad(m->owner->id, 6);
        else
            terminal_write_pad("-", 6);
        terminal_write_dec_pad(waiters, 8);
        terminal_write_dec_pad(m->acquisitions, 8);
        terminal_write_dec(m->contentions);
        terminal_write("\n");
    }
    irq_restore(flags);

    terminal_write("lockdep order violations: ");
    terminal_write_dec(order_violations);
    terminal_write("\n");
}

void sync_reset_order_violations(void)
{
    order_violations = 0;
}