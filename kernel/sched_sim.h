#ifndef SEVENTHOS_SCHED_SIM_H
#define SEVENTHOS_SCHED_SIM_H

#include <stdint.h>

#define SIM_MAX_JOBS 8u
#define SIM_MAX_SEGS 96u

typedef enum
{
    SIM_FCFS = 0,
    SIM_SJF,
    SIM_RR,
    SIM_ALGO_COUNT
} sim_algo_t;

typedef struct
{
    uint32_t arrival;
    uint32_t burst;
} sim_job_t;

typedef struct
{
    uint32_t job;
    uint32_t start;
    uint32_t end;
} sim_segment_t;

typedef struct
{
    uint32_t start_time[SIM_MAX_JOBS]; // pertama kali berjalan
    uint32_t finish[SIM_MAX_JOBS];
    uint32_t waiting[SIM_MAX_JOBS];
    uint32_t turnaround[SIM_MAX_JOBS];
    uint32_t response[SIM_MAX_JOBS];
    sim_segment_t seg[SIM_MAX_SEGS]; // diagram Gantt
    uint32_t seg_count;
    uint32_t total_time;
    uint32_t switches;
    uint32_t sum_wait, sum_turn, sum_resp;
} sim_result_t;

// 0 = sukses, -1 = beban kerja tidak valid, -2 = terlalu banyak irisan waktu (naikkan quantum)
int sim_run(sim_algo_t algo, const sim_job_t *jobs, uint32_t n,
            uint32_t quantum, sim_result_t *out);
const char *sim_algo_name(sim_algo_t algo);

#endif