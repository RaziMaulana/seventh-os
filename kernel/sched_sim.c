#include <stddef.h>
#include <stdint.h>

#include "kstring.h"
#include "sched_sim.h"

#define QN (SIM_MAX_JOBS + 1u) // kapasitas antrean melingkar (n job + 1 slot kosong)

const char *sim_algo_name(sim_algo_t algo)
{
    static const char *const names[] = {"FCFS", "SJF", "RR"};

    return ((unsigned)algo < SIM_ALGO_COUNT) ? names[algo] : "?";
}

// Irisan berurutan milik job yang sama digabung (tidak dihitung sebagai context switch).
static int add_segment(sim_result_t *r, uint32_t job, uint32_t start, uint32_t end)
{
    if (r->seg_count > 0)
    {
        sim_segment_t *last = &r->seg[r->seg_count - 1u];

        if (last->job == job && last->end == start)
        {
            last->end = end;
            return 0;
        }
    }
    if (r->seg_count >= SIM_MAX_SEGS)
        return -1;
    r->seg[r->seg_count].job = job;
    r->seg[r->seg_count].start = start;
    r->seg[r->seg_count].end = end;
    r->seg_count++;
    return 0;
}

// Masukkan semua job yang sudah tiba (arrival <= t) ke antrean, urut kedatangan.
static void rr_admit(const sim_job_t *jobs, uint32_t n, uint8_t *queued,
                     uint32_t *queue, uint32_t *tail, uint32_t t)
{
    for (;;)
    {
        int best = -1;

        for (uint32_t i = 0; i < n; i++)
            if (!queued[i] && jobs[i].arrival <= t &&
                (best < 0 || jobs[i].arrival < jobs[best].arrival))
                best = (int)i;
        if (best < 0)
            return;
        queued[best] = 1;
        queue[*tail] = (uint32_t)best;
        *tail = (*tail + 1u) % QN;
    }
}

int sim_run(sim_algo_t algo, const sim_job_t *jobs, uint32_t n, uint32_t quantum, sim_result_t *r)
{
    uint32_t rem[SIM_MAX_JOBS];
    uint8_t started[SIM_MAX_JOBS], done[SIM_MAX_JOBS], queued[SIM_MAX_JOBS];
    uint32_t queue[QN];
    uint32_t head = 0, tail = 0, t = 0, finished = 0;

    memset(r, 0, sizeof(*r));
    if (n == 0 || n > SIM_MAX_JOBS || (algo == SIM_RR && quantum == 0))
        return -1;
    for (uint32_t i = 0; i < n; i++)
    {
        if (jobs[i].burst == 0)
            return -1;
        rem[i] = jobs[i].burst;
        started[i] = 0;
        done[i] = 0;
        queued[i] = 0;
    }
    if (algo == SIM_RR)
        rr_admit(jobs, n, queued, queue, &tail, t);

    while (finished < n)
    {
        int pick = -1;
        uint32_t run;

        if (algo == SIM_RR)
        {
            if (head == tail)
            { // antrean kosong: CPU idle sampai ada yang tiba
                uint32_t next = 0xFFFFFFFFu;

                for (uint32_t i = 0; i < n; i++)
                    if (!queued[i] && jobs[i].arrival < next)
                        next = jobs[i].arrival;
                t = next;
                rr_admit(jobs, n, queued, queue, &tail, t);
                continue;
            }
            pick = (int)queue[head];
            head = (head + 1u) % QN;
            run = (rem[pick] < quantum) ? rem[pick] : quantum;
        }
        else
        {
            for (uint32_t i = 0; i < n; i++)
            {
                if (done[i] || jobs[i].arrival > t)
                    continue;
                if (pick < 0 ||
                    (algo == SIM_FCFS && jobs[i].arrival < jobs[pick].arrival) ||
                    (algo == SIM_SJF && jobs[i].burst < jobs[pick].burst))
                    pick = (int)i;
            }
            if (pick < 0)
            { // idle: lompat ke kedatangan berikutnya
                uint32_t next = 0xFFFFFFFFu;

                for (uint32_t i = 0; i < n; i++)
                    if (!done[i] && jobs[i].arrival < next)
                        next = jobs[i].arrival;
                t = next;
                continue;
            }
            run = rem[pick]; // non-preemptive: jalan sampai selesai
        }

        if (!started[pick])
        {
            started[pick] = 1;
            r->start_time[pick] = t;
        }
        if (add_segment(r, (uint32_t)pick, t, t + run) != 0)
            return -2;
        t += run;
        rem[pick] -= run;

        if (algo == SIM_RR)
            rr_admit(jobs, n, queued, queue, &tail, t); // yang tiba SAAT ini masuk sebelum job yang di-preempt

        if (rem[pick] > 0)
        {
            queue[tail] = (uint32_t)pick;
            tail = (tail + 1u) % QN;
        }
        else
        {
            r->finish[pick] = t;
            done[pick] = 1;
            finished++;
        }
    }

    for (uint32_t i = 0; i < n; i++)
    {
        r->turnaround[i] = r->finish[i] - jobs[i].arrival;
        r->waiting[i] = r->turnaround[i] - jobs[i].burst;
        r->response[i] = r->start_time[i] - jobs[i].arrival;
        r->sum_wait += r->waiting[i];
        r->sum_turn += r->turnaround[i];
        r->sum_resp += r->response[i];
    }
    r->total_time = t;
    r->switches = (r->seg_count > 0) ? r->seg_count - 1u : 0;
    return 0;
}