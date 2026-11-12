#ifndef SEVENTHOS_THREAD_H
#define SEVENTHOS_THREAD_H

#include <stdint.h>

struct mutex;
struct wait_queue;
struct process;

#define THREAD_MAX_HELD_LOCKS 4u

typedef enum
{
    THREAD_NEW = 0,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_WAITING,
    THREAD_TERMINATED
} thread_state_t;

typedef void (*thread_entry_t)(void *arg);

typedef struct thread
{
    uint32_t id;
    char name[16];
    thread_state_t state;
    uint32_t priority;
    uint32_t esp;
    uint8_t *stack_base;
    uint32_t stack_size;
    thread_entry_t entry;
    void *arg;
    uint32_t time_slice;
    uint32_t ticks_left;
    uint32_t cpu_ticks;
    uint32_t switches;
    uint32_t wake_tick;
    uint8_t sleeping;
    uint8_t is_idle;
    uint32_t arrival_tick;  
    uint32_t first_run_tick;
    uint32_t finish_tick; 
    uint8_t ran_once;
    uint32_t cpu_limit;
    uint32_t kstack_top;
    struct mutex *waiting_on;
    struct wait_queue *wait_queue;
    struct thread *wait_next;
    struct mutex *held_locks[THREAD_MAX_HELD_LOCKS];
    uint8_t held_count;
    struct process *process;
    struct thread *next;
} thread_t;

#define THREAD_DEFAULT_SLICE 5u

extern thread_t *g_current_thread;

void threads_initialize(void);
thread_t *thread_create(const char *name, thread_entry_t entry, void *arg, uint32_t priority);
void thread_start(thread_t *t);
void thread_exit(void);
int thread_kill(uint32_t id);
void thread_yield(void);
void thread_sleep_ms(uint32_t ms);
void thread_block(void);
void thread_unblock(thread_t *t);
int thread_set_priority(uint32_t id, uint32_t priority);
void thread_reap(void);

void thread_wake_sleepers(uint32_t now);

thread_t *thread_first(void);
thread_t *thread_idle(void);
uint32_t thread_count(void);
const char *thread_state_name(thread_state_t state);
void thread_log_print(void);

#endif