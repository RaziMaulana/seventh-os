#ifndef SEVENTHOS_SYNC_DEMO_H
#define SEVENTHOS_SYNC_DEMO_H

#include <stdint.h>

typedef enum { RACE_NONE = 0, RACE_MUTEX, RACE_SEM } race_mode_t;

typedef struct {
    uint32_t expected;     // 2 x iterasi
    uint32_t actual;       // nilai akhir counter
    uint32_t lost;         // update yang hilang
    uint32_t blocked;      // berapa kali thread harus menunggu kunci
    uint32_t ticks;        // lama pengujian
    uint32_t switches;     // context switch selama pengujian
} race_result_t;

int race_run(race_mode_t mode, uint32_t iterations, race_result_t *result);   // 0 = sukses
int deadlock_demo(int safe);                                                   // Langkah 4

#endif