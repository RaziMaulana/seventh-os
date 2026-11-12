#ifndef SEVENTHOS_SYNC_H
#define SEVENTHOS_SYNC_H

#include <stdint.h>
#include "thread.h"

typedef struct wait_queue
{
    thread_t *head;
    thread_t *tail;
} wait_queue_t;

typedef struct mutex
{
    const char *name;
    uint32_t rank; // urutan global: kunci rank kecil lebih dulu
    uint8_t locked;
    thread_t *owner;
    wait_queue_t waiters;
    uint32_t acquisitions;
    uint32_t contentions; // berapa kali thread harus menunggu
} mutex_t;

typedef struct semaphore
{
    const char *name;
    int32_t count;
    wait_queue_t waiters;
    uint32_t waits;
    uint32_t blocks;
} semaphore_t;

void mutex_init(mutex_t *m, const char *name, uint32_t rank);
void mutex_lock(mutex_t *m);
int mutex_trylock(mutex_t *m); // 0 = berhasil, -1 = sedang dipegang
void mutex_unlock(mutex_t *m);
void mutex_lock_pair(mutex_t *a, mutex_t *b); // selalu mengunci sesuai rank (anti circular wait)
void mutex_unlock_pair(mutex_t *a, mutex_t *b);

void sem_init(semaphore_t *s, const char *name, int32_t initial);
void sem_wait(semaphore_t *s);
void sem_post(semaphore_t *s);

uint32_t sync_detect_deadlock(void); // mencetak siklus; return jumlah siklus
void sync_print_locks(void);
void sync_reset_order_violations(void);
void sync_thread_cleanup(thread_t *t); // dipanggil saat thread berakhir/dibunuh

#endif