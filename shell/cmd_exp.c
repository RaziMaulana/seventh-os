#include <stddef.h>
#include <stdint.h>

#include "cmd_exp.h"
#include "cpu.h"
#include "fit_arena.h"
#include "kstring.h"
#include "sched_sim.h"
#include "scheduler.h"
#include "shell_util.h"
#include "terminal.h"
#include "thread.h"
#include "timer.h"

typedef struct
{
    sim_job_t jobs[SIM_MAX_JOBS];
    uint32_t n;
    uint32_t quantum;
    int algo; // -1 = bandingkan ketiganya
    int trace;
} sim_args_t;

static sim_args_t sargs; // static: hemat stack shell
static sim_result_t sres[SIM_ALGO_COUNT];

static int parse_sim_args(const char *s, sim_args_t *a)
{
    static const uint32_t defaults[4][2] = {{0, 8}, {1, 4}, {2, 9}, {3, 5}};

    a->n = 0;
    a->quantum = 3;
    a->algo = -1;
    a->trace = 0;

    while (*s != '\0')
    {
        while (*s == ' ')
            s++;
        if (*s == '\0')
            break;

        if (word_is(s, "fcfs"))
        {
            a->algo = SIM_FCFS;
            s += 4;
        }
        else if (word_is(s, "sjf"))
        {
            a->algo = SIM_SJF;
            s += 3;
        }
        else if (word_is(s, "rr"))
        {
            a->algo = SIM_RR;
            s += 2;
        }
        else if (word_is(s, "trace"))
        {
            a->trace = 1;
            s += 5;
        }
        else if (s[0] == 'q' && s[1] == '=')
        {
            s += 2;
            if (!parse_uint(&s, &a->quantum) || a->quantum == 0 || a->quantum > 100u)
                return -1;
        }
        else
        {
            uint32_t arrival, burst;

            if (!parse_uint(&s, &arrival) || *s != ':')
                return -1;
            s++;
            if (!parse_uint(&s, &burst) || burst == 0 || burst > 99u || a->n >= SIM_MAX_JOBS)
                return -1;
            a->jobs[a->n].arrival = arrival;
            a->jobs[a->n].burst = burst;
            a->n++;
        }
    }

    if (a->n == 0)
    {
        for (uint32_t i = 0; i < 4; i++)
        {
            a->jobs[i].arrival = defaults[i][0];
            a->jobs[i].burst = defaults[i][1];
        }
        a->n = 4;
    }
    return 0;
}

static void print_tenths_pad(uint32_t value, uint32_t width)
{
    uint32_t len = 3; // "d.d"

    for (uint32_t t = value / 10u; t >= 10u; t /= 10u)
        len++;
    print_tenths(value);
    for (; len < width; len++)
        terminal_putchar(' ');
}

static void print_workload(const sim_args_t *a)
{
    terminal_write("Workload:");
    for (uint32_t i = 0; i < a->n; i++)
    {
        terminal_write(" P");
        terminal_write_dec(i + 1u);
        terminal_write("(arr ");
        terminal_write_dec(a->jobs[i].arrival);
        terminal_write(",burst ");
        terminal_write_dec(a->jobs[i].burst);
        terminal_write(")");
    }
    terminal_write("  q=");
    terminal_write_dec(a->quantum);
    terminal_write("\n");
}

// Satu karakter = satu satuan waktu. Digit = nomor job, '.' = CPU idle.
static void print_gantt(const char *label, const sim_result_t *r)
{
    char line[72];
    uint32_t width = (r->total_time < 70u) ? r->total_time : 70u;

    memset(line, '.', sizeof(line));
    for (uint32_t s = 0; s < r->seg_count; s++)
        for (uint32_t t = r->seg[s].start; t < r->seg[s].end && t < width; t++)
            line[t] = (char)('1' + r->seg[s].job);

    terminal_write_pad(label, 6);
    terminal_putchar('|');
    for (uint32_t t = 0; t < width; t++)
        terminal_putchar(line[t]);
    terminal_write(r->total_time > width ? "+\n" : "|\n");
}

static void print_ruler(uint32_t total_time)
{
    uint32_t width = (total_time < 70u) ? total_time : 70u;

    terminal_write_pad("", 7);
    for (uint32_t t = 0; t < width; t += 5u)
        terminal_write_dec_pad(t, 5);
    terminal_putchar('\n');
}

static void print_job_table(const sim_args_t *a, const sim_result_t *r)
{
    terminal_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    terminal_write("JOB  ARRIVAL BURST START FINISH WAIT TURNAROUND RESPONSE\n");
    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);

    for (uint32_t i = 0; i < a->n; i++)
    {
        terminal_write("P");
        terminal_write_dec_pad(i + 1u, 4);
        terminal_write_dec_pad(a->jobs[i].arrival, 8);
        terminal_write_dec_pad(a->jobs[i].burst, 6);
        terminal_write_dec_pad(r->start_time[i], 6);
        terminal_write_dec_pad(r->finish[i], 7);
        terminal_write_dec_pad(r->waiting[i], 5);
        terminal_write_dec_pad(r->turnaround[i], 11);
        terminal_write_dec(r->response[i]);
        terminal_write("\n");
    }
    terminal_write("avg WAIT=");
    print_tenths(tenths(r->sum_wait, a->n));
    terminal_write("  TURNAROUND=");
    print_tenths(tenths(r->sum_turn, a->n));
    terminal_write("  RESPONSE=");
    print_tenths(tenths(r->sum_resp, a->n));
    terminal_write("  switches=");
    terminal_write_dec(r->switches);
    terminal_write("\n");
}

/* ---------- jejak perubahan status thread ---------- */

typedef struct
{
    uint32_t time;
    uint8_t key, job, from, to;
} sim_event_t;

#define EV_MAX 64u

static void add_event(sim_event_t *ev, uint32_t *count, uint32_t time,
                      uint8_t key, uint32_t job, uint8_t from, uint8_t to)
{
    if (*count >= EV_MAX)
        return;
    ev[*count].time = time;
    ev[*count].key = key;
    ev[*count].job = (uint8_t)job;
    ev[*count].from = from;
    ev[*count].to = to;
    (*count)++;
}

static int event_before(const sim_event_t *x, const sim_event_t *y)
{
    if (x->time != y->time)
        return x->time < y->time;
    if (x->key != y->key)
        return x->key < y->key;
    return x->job < y->job;
}

// Status: 0 NEW, 1 READY, 2 RUN, 3 TERM. Urutan di instan yang sama:
// kedatangan (key 0), lalu pelepasan CPU (key 1), lalu pemberian CPU (key 2).
static void print_trace(const sim_args_t *a, const sim_result_t *r)
{
    static const char *const names[] = {"NEW", "READY", "RUN", "TERM"};
    sim_event_t ev[EV_MAX];
    uint32_t count = 0;

    for (uint32_t i = 0; i < a->n; i++)
        add_event(ev, &count, a->jobs[i].arrival, 0, i, 0, 1);
    for (uint32_t s = 0; s < r->seg_count; s++)
    {
        uint32_t j = r->seg[s].job;

        add_event(ev, &count, r->seg[s].start, 2, j, 1, 2);
        add_event(ev, &count, r->seg[s].end, 1, j, 2, (r->finish[j] == r->seg[s].end) ? 3 : 1);
    }

    for (uint32_t i = 1; i < count; i++)
    { // insertion sort
        sim_event_t cur = ev[i];
        uint32_t k = i;

        while (k > 0 && event_before(&cur, &ev[k - 1u]))
        {
            ev[k] = ev[k - 1u];
            k--;
        }
        ev[k] = cur;
    }

    terminal_write("Thread state changes (time: job from>to):\n");
    uint32_t last = 0xFFFFFFFFu;

    for (uint32_t k = 0; k < count; k++)
    {
        if (ev[k].time != last)
        {
            if (last != 0xFFFFFFFFu)
                terminal_putchar('\n');
            terminal_write("t=");
            terminal_write_dec_pad(ev[k].time, 3);
            last = ev[k].time;
        }
        terminal_write(" P");
        terminal_write_dec(ev[k].job + 1u);
        terminal_putchar(' ');
        terminal_write(names[ev[k].from]);
        terminal_putchar('>');
        terminal_write(names[ev[k].to]);
        terminal_write(" ");
    }
    terminal_putchar('\n');
}

void cmd_schedsim(const char *args)
{
    sim_args_t *a = &sargs;

    if (parse_sim_args(args, a) != 0)
    {
        terminal_write("usage: schedsim [fcfs|sjf|rr] [trace] [q=N] [arrival:burst ...]\n"
                       "  e.g. schedsim rr trace q=2 0:5 1:3 2:8\n");
        return;
    }

    uint32_t first = (a->algo >= 0) ? (uint32_t)a->algo : 0u;
    uint32_t last = (a->algo >= 0) ? (uint32_t)a->algo : (uint32_t)SIM_ALGO_COUNT - 1u;

    for (uint32_t k = first; k <= last; k++)
    {
        int rc = sim_run((sim_algo_t)k, a->jobs, a->n, a->quantum, &sres[k]);

        if (rc != 0)
        {
            terminal_write(rc == -2 ? "too many time slices: increase q=\n" : "invalid workload\n");
            return;
        }
    }

    print_workload(a);

    if (a->algo >= 0)
    { // satu algoritma: detail
        const sim_result_t *r = &sres[a->algo];

        if (a->trace)
        {
            print_gantt(sim_algo_name((sim_algo_t)a->algo), r);
            print_ruler(r->total_time);
            print_trace(a, r);
        }
        else
        {
            print_job_table(a, r);
            print_gantt(sim_algo_name((sim_algo_t)a->algo), r);
            print_ruler(r->total_time);
        }
        return;
    }

    uint32_t longest = 0; // ketiganya: perbandingan
    uint32_t best_wait = 0, best_resp = 0;

    for (uint32_t k = 0; k < SIM_ALGO_COUNT; k++)
    {
        print_gantt(sim_algo_name((sim_algo_t)k), &sres[k]);
        if (sres[k].total_time > longest)
            longest = sres[k].total_time;
        if (sres[k].sum_wait < sres[best_wait].sum_wait)
            best_wait = k;
        if (sres[k].sum_resp < sres[best_resp].sum_resp)
            best_resp = k;
    }
    print_ruler(longest);

    terminal_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    terminal_write("ALGO  AVG-WAIT AVG-TURN AVG-RESP SWITCHES TOTAL\n");
    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    for (uint32_t k = 0; k < SIM_ALGO_COUNT; k++)
    {
        terminal_write_pad(sim_algo_name((sim_algo_t)k), 6);
        print_tenths_pad(tenths(sres[k].sum_wait, a->n), 9);
        print_tenths_pad(tenths(sres[k].sum_turn, a->n), 9);
        print_tenths_pad(tenths(sres[k].sum_resp, a->n), 9);
        terminal_write_dec_pad(sres[k].switches, 9);
        terminal_write_dec(sres[k].total_time);
        terminal_write("\n");
    }
    terminal_write("Lowest avg waiting: ");
    terminal_write(sim_algo_name((sim_algo_t)best_wait));
    terminal_write("   lowest avg response: ");
    terminal_write(sim_algo_name((sim_algo_t)best_resp));
    terminal_write("\n");
}

#define LIVE_MAX 6u

static void burn_forever(void *arg)
{
    (void)arg;
    for (;;)
    {
    } // CPU-bound murni; scheduler_tick mengakhirinya saat cpu_limit tercapai
}

static uint32_t absdiff(uint32_t a, uint32_t b)
{
    return (a > b) ? a - b : b - a;
}

void cmd_schedlive(const char *args)
{
    static const char *const names[LIVE_MAX] = {"job1", "job2", "job3", "job4", "job5", "job6"};
    static const uint32_t default_burst[4] = {40, 20, 45, 25};
    uint32_t burst[LIVE_MAX];
    thread_t *w[LIVE_MAX];
    sim_job_t jobs[LIVE_MAX];
    uint32_t n = 0, total = 0, busy = 0;

    while (*args != '\0')
    {
        uint32_t v;

        while (*args == ' ')
            args++;
        if (*args == '\0')
            break;
        if (!parse_uint(&args, &v) || v == 0 || v > 200u || n >= LIVE_MAX)
        {
            terminal_write("usage: schedlive [burst ...]   (1-6 jobs, each 1-200 ticks)\n");
            return;
        }
        burst[n++] = v;
    }
    if (n == 0)
    {
        for (uint32_t i = 0; i < 4; i++)
            burst[i] = default_burst[i];
        n = 4;
    }
    for (uint32_t i = 0; i < n; i++)
        total += burst[i];

    // Pengukuran hanya valid bila tidak ada thread lain yang berebut CPU.
    uint32_t flags = irq_save();
    for (thread_t *t = thread_first(); t != NULL; t = t->next)
        if (t != g_current_thread && !t->is_idle &&
            (t->state == THREAD_NEW || t->state == THREAD_READY || t->state == THREAD_RUNNING))
            busy++;
    irq_restore(flags);
    if (busy != 0)
    {
        terminal_write("schedlive: other runnable threads exist (see 'ps'); stop them first: kill <id>\n");
        return;
    }

    for (uint32_t i = 0; i < n; i++)
    {
        w[i] = thread_create(names[i], burn_forever, NULL, 5);
        if (w[i] == NULL)
        {
            for (uint32_t j = 0; j < i; j++)
                thread_kill(w[j]->id);
            thread_reap();
            terminal_write("schedlive: out of memory\n");
            return;
        }
        w[i]->cpu_limit = burst[i];
    }

    sched_policy_t old_policy = scheduler_get_policy();
    scheduler_set_policy(SCHED_ROUND_ROBIN);

    uint32_t edge = timer_get_ticks();
    while (timer_get_ticks() == edge)
    {
    } // sinkron ke batas tick supaya semua job tiba bersamaan

    flags = irq_save();
    for (uint32_t i = 0; i < n; i++)
        thread_start(w[i]); // arrival_tick identik: interrupt nonaktif
    uint32_t t0 = timer_get_ticks();
    irq_restore(flags);

    int timed_out = 0;
    for (;;)
    {
        int alive = 0;

        for (uint32_t i = 0; i < n; i++)
            if (w[i]->state != THREAD_TERMINATED)
                alive = 1;
        if (!alive)
            break;
        if (timer_get_ticks() - t0 > total * 2u + 200u)
        {
            timed_out = 1;
            break;
        }
        thread_sleep_ms(50); // shell tidur; job memakai seluruh CPU
    }
    scheduler_set_policy(old_policy);

    if (timed_out)
    {
        for (uint32_t i = 0; i < n; i++)
            thread_kill(w[i]->id);
        thread_reap();
        terminal_write("schedlive: timeout, jobs aborted\n");
        return;
    }

    for (uint32_t i = 0; i < n; i++)
    {
        jobs[i].arrival = 0;
        jobs[i].burst = burst[i];
    }
    if (sim_run(SIM_RR, jobs, n, THREAD_DEFAULT_SLICE, &sres[SIM_RR]) != 0)
    {
        terminal_write("schedlive: simulation failed\n");
        thread_reap();
        return;
    }

    terminal_write("Live Round Robin: quantum ");
    terminal_write_dec(THREAD_DEFAULT_SLICE);
    terminal_write(" ticks, ");
    terminal_write_dec(n);
    terminal_write(" CPU-bound jobs arriving together (measured from TCB timestamps)\n");
    terminal_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    terminal_write("JOB  BURST  WAIT  TURN  RESP  | S-WAIT S-TURN S-RESP DIFF\n");
    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);

    uint32_t max_diff = 0, sum_w = 0, sum_t = 0, sum_r = 0, last_finish = 0;

    for (uint32_t i = 0; i < n; i++)
    {
        uint32_t turn = w[i]->finish_tick - w[i]->arrival_tick;
        uint32_t resp = w[i]->first_run_tick - w[i]->arrival_tick;
        uint32_t wait = (turn > w[i]->cpu_ticks) ? turn - w[i]->cpu_ticks : 0u;
        const sim_result_t *s = &sres[SIM_RR];
        uint32_t diff = absdiff(wait, s->waiting[i]);

        if (absdiff(turn, s->turnaround[i]) > diff)
            diff = absdiff(turn, s->turnaround[i]);
        if (absdiff(resp, s->response[i]) > diff)
            diff = absdiff(resp, s->response[i]);
        if (diff > max_diff)
            max_diff = diff;

        sum_w += wait;
        sum_t += turn;
        sum_r += resp;
        if (w[i]->finish_tick > last_finish)
            last_finish = w[i]->finish_tick;

        terminal_write("P");
        terminal_write_dec_pad(i + 1u, 4);
        terminal_write_dec_pad(burst[i], 7);
        terminal_write_dec_pad(wait, 6);
        terminal_write_dec_pad(turn, 6);
        terminal_write_dec_pad(resp, 6);
        terminal_write("| ");
        terminal_write_dec_pad(s->waiting[i], 7);
        terminal_write_dec_pad(s->turnaround[i], 7);
        terminal_write_dec_pad(s->response[i], 7);
        terminal_write_dec(diff);
        terminal_write("\n");
    }

    terminal_write("measured avg WAIT=");
    print_tenths(tenths(sum_w, n));
    terminal_write("  TURNAROUND=");
    print_tenths(tenths(sum_t, n));
    terminal_write("  RESPONSE=");
    print_tenths(tenths(sum_r, n));
    terminal_write("  elapsed=");
    terminal_write_dec(last_finish - t0);
    terminal_write(" ticks\n");

    if (max_diff == 0)
        terminal_write("Measured == simulated: the kernel scheduler follows the Round Robin model exactly.\n");
    else if (max_diff <= 1)
        terminal_write("Within 1 tick of the model (timer phase alignment). Run again to confirm.\n");
    else
        terminal_write("Large difference: another thread competed for the CPU. Check 'ps'.\n");

    thread_reap(); // bebaskan TCB dan stack job yang sudah TERMINATED
}

#define FIT_OPS_MAX 200u
#define FIT_LIVE_MAX 12u

typedef struct
{
    uint8_t type;
    uint8_t id;
    uint8_t size;
} fit_op_t; // type 0 = alloc, 1 = free

static fit_arena_t arenas[FIT_COUNT];
static fit_op_t fit_ops[FIT_OPS_MAX];
static char map_line[FIT_ARENA_UNITS + 1u];

static uint32_t rng_next(uint32_t *state)
{
    *state = *state * 1103515245u + 12345u;
    return (*state >> 16) & 0x7FFFu;
}

// Urutan operasi dibuat SEKALI dan dipakai ulang untuk ketiga strategi, jadi
// perbedaan hasil murni berasal dari cara memilih lubang.
static void generate_ops(uint32_t count, uint32_t seed)
{
    uint8_t live[FIT_LIVE_MAX];
    uint32_t nlive = 0;
    uint8_t next_id = 1;

    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t r = rng_next(&seed);

        if (nlive < 3u || (nlive < FIT_LIVE_MAX && (r % 100u) < 58u))
        {
            fit_ops[i].type = 0;
            fit_ops[i].id = next_id;
            fit_ops[i].size = (uint8_t)(2u + rng_next(&seed) % 15u); // 2..16 unit
            live[nlive++] = next_id++;
        }
        else
        {
            uint32_t k = rng_next(&seed) % nlive;

            fit_ops[i].type = 1;
            fit_ops[i].id = live[k];
            fit_ops[i].size = 0;
            live[k] = live[--nlive];
        }
    }
}

// Peta memori 128 unit dalam dua baris. '.' = bebas, A-Z = alokasi, '#' = tercadang.
static void print_map(const char *label, const fit_arena_t *a)
{
    fit_render_map(a, map_line);
    for (uint32_t row = 0; row < 2u; row++)
    {
        terminal_write_pad(row == 0 ? label : "", 10);
        terminal_putchar('|');
        for (uint32_t i = 0; i < 64u; i++)
            terminal_putchar(map_line[row * 64u + i]);
        terminal_write("|\n");
    }
}

static int has_prefix(const char *s, const char *prefix)
{
    while (*prefix != '\0')
        if (*s++ != *prefix++)
            return 0;
    return 1;
}

static void fit_classic(void)
{
    static const uint32_t requests[4] = {11, 20, 7, 21};
    int result[FIT_COUNT][4];

    fit_setup_classic(&arenas[0]);
    terminal_write("Textbook scenario: holes of 6, 25, 10, 15, 30 units; requests 11, 20, 7, 21\n");
    print_map("initial", &arenas[0]);

    for (uint32_t s = 0; s < FIT_COUNT; s++)
    {
        fit_setup_classic(&arenas[s]);
        for (uint32_t r = 0; r < 4u; r++)
            result[s][r] = fit_alloc(&arenas[s], (fit_strategy_t)s, requests[r], (uint8_t)(r + 1u));
    }

    terminal_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    terminal_write("STRATEGY   req 11  req 20  req 7   req 21  PLACED\n");
    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);

    for (uint32_t s = 0; s < FIT_COUNT; s++)
    {
        terminal_write_pad(fit_strategy_name((fit_strategy_t)s), 11);
        for (uint32_t r = 0; r < 4u; r++)
        {
            if (result[s][r] >= 0)
            {
                terminal_putchar('@');
                terminal_write_dec_pad((uint32_t)result[s][r], 7);
            }
            else
            {
                terminal_write_pad("FAIL", 8);
            }
        }
        terminal_write_dec(arenas[s].allocs_ok);
        terminal_write("/4\n");
    }

    terminal_write("Final memory:\n");
    for (uint32_t s = 0; s < FIT_COUNT; s++)
        print_map(fit_strategy_name((fit_strategy_t)s), &arenas[s]);
    terminal_write("@N = allocation placed at unit N. Same requests, different holes chosen.\n");
}

static void fit_random(uint32_t ops, uint32_t seed)
{
    fit_stats_t st[FIT_COUNT];
    uint32_t best_frag = 0, best_steps = 0;

    generate_ops(ops, seed);
    for (uint32_t s = 0; s < FIT_COUNT; s++)
    {
        fit_init(&arenas[s]);
        for (uint32_t i = 0; i < ops; i++)
        {
            if (fit_ops[i].type == 0)
                fit_alloc(&arenas[s], (fit_strategy_t)s, fit_ops[i].size, fit_ops[i].id);
            else
                fit_free(&arenas[s], fit_ops[i].id); // no-op bila alokasinya dulu gagal
        }
        fit_get_stats(&arenas[s], &st[s]);
    }

    terminal_write("Random workload: ");
    terminal_write_dec(ops);
    terminal_write(" ops, seed ");
    terminal_write_dec(seed);
    terminal_write(", arena ");
    terminal_write_dec(FIT_ARENA_UNITS);
    terminal_write(" units, requests 2..16 units\n");
    for (uint32_t s = 0; s < FIT_COUNT; s++)
        print_map(fit_strategy_name((fit_strategy_t)s), &arenas[s]);

    terminal_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    terminal_write("STRATEGY   OK   FAIL-FRAG FAIL-FULL FREE HOLES LARGEST FRAG% STEPS\n");
    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);

    for (uint32_t s = 0; s < FIT_COUNT; s++)
    {
        terminal_write_pad(fit_strategy_name((fit_strategy_t)s), 11);
        terminal_write_dec_pad(arenas[s].allocs_ok, 5);
        terminal_write_dec_pad(arenas[s].fail_frag, 10);
        terminal_write_dec_pad(arenas[s].fail_full, 10);
        terminal_write_dec_pad(st[s].free_units, 5);
        terminal_write_dec_pad(st[s].holes, 6);
        terminal_write_dec_pad(st[s].largest_free, 8);
        terminal_write_dec_pad(st[s].frag_percent, 6);
        terminal_write_dec(arenas[s].steps);
        terminal_write("\n");

        if (arenas[s].fail_frag < arenas[best_frag].fail_frag)
            best_frag = s;
        if (arenas[s].steps < arenas[best_steps].steps)
            best_steps = s;
    }
    terminal_write("Fewest fragmentation failures: ");
    terminal_write(fit_strategy_name((fit_strategy_t)best_frag));
    terminal_write("   cheapest search: ");
    terminal_write(fit_strategy_name((fit_strategy_t)best_steps));
    terminal_write("\nFAIL-FRAG = enough total free memory, but no single hole large enough.\n");
}

void cmd_fitexp(const char *args)
{
    uint32_t seed = 5, ops = 100;
    int classic = 0;

    while (*args != '\0')
    {
        while (*args == ' ')
            args++;
        if (*args == '\0')
            break;
        if (word_is(args, "classic"))
        {
            classic = 1;
            args += 7;
        }
        else if (has_prefix(args, "seed="))
        {
            args += 5;
            if (!parse_uint(&args, &seed))
                goto usage;
        }
        else if (has_prefix(args, "ops="))
        {
            args += 4;
            if (!parse_uint(&args, &ops))
                goto usage;
        }
        else
        {
            goto usage;
        }
    }
    if (ops < 10u || ops > FIT_OPS_MAX)
        goto usage;

    if (classic)
        fit_classic();
    else
        fit_random(ops, seed);
    return;

usage:
    terminal_write("usage: fitexp [classic] [seed=N] [ops=10..200]\n");
}