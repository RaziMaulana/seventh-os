#ifndef SEVENTHOS_SCHEDULER_H
#define SEVENTHOS_SCHEDULER_H

#include <stdint.h>

typedef enum
{
    SCHED_ROUND_ROBIN = 0,
    SCHED_PRIORITY
} sched_policy_t;

void scheduler_start(void); // daftarkan callback timer, aktifkan
void scheduler_tick(void);  // dipanggil tiap IRQ0
void schedule(void);        // WAJIB dipanggil dengan interrupt nonaktif
void scheduler_set_policy(sched_policy_t policy);
sched_policy_t scheduler_get_policy(void);
const char *scheduler_policy_name(void);
uint32_t scheduler_get_switches(void);

#endif