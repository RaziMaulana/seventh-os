#include <stddef.h>
#include <stdint.h>

#include "cmd_fs.h"
#include "cpu.h"
#include "gdt.h"
#include "heap.h"
#include "kstring.h"
#include "multiboot.h"
#include "paging.h"
#include "pmm.h"
#include "process.h"
#include "scheduler.h"
#include "selftest.h"
#include "shell.h"
#include "stats.h"
#include "sync.h"
#include "sync_demo.h"
#include "terminal.h"
#include "thread.h"
#include "timer.h"
#include "vfs.h"

extern char __kernel_start[];

#define REQ_COUNT 8u

static const char *const req_names[REQ_COUNT] = {
    "Boot + kernel entry (GRUB, Multiboot, C kernel)",
    "Interrupts, timer IRQ, keyboard, shell",
    "Memory: PMM, paging, kernel heap, OOM handling",
    "Threads + preemptive CPU scheduling",
    "Synchronization (mutex, semaphore, race condition)",
    "Deadlock (detection and lock ordering)",
    "User mode (Ring 3) and memory protection",
    "Filesystem (RAMDisk, VFS, CRUD)",
};

static uint32_t req_pass[REQ_COUNT];
static uint32_t req_fail[REQ_COUNT];
static int verbose;

static volatile uint32_t cnt_a, cnt_b;
static mutex_t da, db;
static volatile uint32_t dl_finished;

/* ---------- pembantu ---------- */

static void check(uint32_t req, const char *name, int ok)
{
    if (ok)
        req_pass[req - 1u]++;
    else
        req_fail[req - 1u]++;

    if (!ok || verbose)
    {
        terminal_write("  ");
        terminal_set_color(ok ? COLOR_LIGHT_GREEN : COLOR_LIGHT_RED, COLOR_BLACK);
        terminal_write(ok ? "[PASS] " : "[FAIL] ");
        terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
        terminal_write("R");
        terminal_write_dec(req);
        terminal_write(" ");
        terminal_write(name);
        terminal_write("\n");
    }
}

static int wait_state(thread_t *t, thread_state_t want, uint32_t timeout_ms)
{
    for (uint32_t waited = 0; waited < timeout_ms; waited += 10u)
    {
        if (t->state == want)
            return 1;
        thread_sleep_ms(10);
    }
    return t->state == want;
}

static void kill_both(thread_t *a, thread_t *b)
{
    if (a != NULL)
        thread_kill(a->id); // -1 bila sudah TERMINATED: tidak masalah
    if (b != NULL)
        thread_kill(b->id);
    thread_reap();
}

static void spin_task(void *arg)
{
    (void)arg;
    for (;;)
    {
    }
}

static void counter_task(void *arg)
{
    volatile uint32_t *counter = arg;

    for (;;)
        (*counter)++;
}

/* ---------- R1: boot dan kernel entry ---------- */

static void test_boot(void)
{
    uint32_t cr0, cs;

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cs, %0" : "=r"(cs));
    cs &= 0xFFFFu;

    check(1, "CPU runs in 32-bit protected mode (CR0.PE)", (cr0 & 1u) != 0);
    check(1, "kernel code segment is Ring 0 (CS = 0x08)", cs == KERNEL_CODE_SEL);
    check(1, "kernel image is linked at 1 MiB", (uint32_t)(uintptr_t)__kernel_start == 0x00100000u);

    uint32_t pmm_kib = (uint32_t)(pmm_get_total_pages() * 4u);
    uint32_t pmm_top = (uint32_t)pmm_get_highest_address();
    int mem_ok = pmm_get_total_pages() >= 2048u && pmm_top >= 0x00800000u;
    check(1, "Multiboot memory map parsed (PMM >= 8 MiB, highest RAM >= 8 MiB)", mem_ok);
    if (!mem_ok)
    {
        terminal_write("        PMM usable = ");
        terminal_write_dec(pmm_kib);
        terminal_write(" KiB, highest RAM address = ");
        terminal_write_hex(pmm_top);
        terminal_write(", identity map limit = ");
        terminal_write_hex(paging_get_identity_limit());
        terminal_write("\n");
    }
}

/* ---------- R2: interrupt, timer, shell ---------- */

static void test_interrupts(void)
{
    struct
    {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) idtr;
    uint32_t eflags;

    __asm__ volatile("sidt %0" : "=m"(idtr));
    __asm__ volatile("pushf; pop %0" : "=r"(eflags));

    check(2, "IDT loaded with 256 entries", idtr.limit == 256u * 8u - 1u);
    check(2, "interrupts are enabled (IF = 1)", (eflags & 0x200u) != 0);

    uint32_t t0 = timer_get_ticks();
    uint32_t irq0 = stats_get_irq(0);

    thread_sleep_ms(100);

    uint32_t dt = timer_get_ticks() - t0;

    check(2, "PIT drives ~100 Hz ticks (100 ms sleep = 10..12 ticks)", dt >= 10u && dt <= 12u);
    check(2, "IRQ0 handler counted the interrupts", stats_get_irq(0) - irq0 >= 10u);

    shell_run_command("version");
    shell_run_command("echo selftest");
    shell_run_command("ps");
    check(2, "shell dispatches commands without faulting", 1);
}

/* ---------- R3: memori ---------- */

static void test_memory(void)
{
    uint32_t cr0;
    size_t free0 = pmm_get_free_pages();
    uintptr_t p1 = pmm_alloc_page();
    uintptr_t p2 = pmm_alloc_page();
    int ok = p1 != 0 && p2 != 0 && p1 != p2 && (p1 & 0xFFFu) == 0 && p1 >= 0x00100000u &&
             pmm_get_free_pages() == free0 - 2u;

    if (p1 != 0)
        pmm_free_page(p1);
    if (p2 != 0)
        pmm_free_page(p2);
    check(3, "PMM: distinct aligned pages, exact accounting after alloc/free",
          ok && pmm_get_free_pages() == free0);

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    check(3, "paging enabled (CR0.PG) with write-protect (CR0.WP)",
          paging_is_enabled() && (cr0 & 0x80010000u) == 0x80010000u);

    const uint32_t virt = 0xE0000000u;
    uintptr_t phys = pmm_alloc_page();
    uint32_t translated = 0;
    int map_ok = phys != 0 &&
                 paging_map_page(virt, (uint32_t)phys, PAGE_PRESENT | PAGE_WRITE) == 0;

    if (map_ok)
    {
        *(volatile uint32_t *)virt = 0xCAFEBABEu;             // tulis lewat alamat virtual
        map_ok = *(volatile uint32_t *)phys == 0xCAFEBABEu && // baca lewat alias fisik
                 paging_translate(virt, &translated) == 0 && translated == phys;

        uint32_t old = paging_unmap_page(virt);

        map_ok = map_ok && old == phys && paging_translate(virt, &translated) != 0;
    }
    if (phys != 0)
        pmm_free_page(phys);
    check(3, "paging: map, translate, alias read, unmap", map_ok);

    heap_stats_t h0, h1;
    void *blocks[8];
    int heap_ok = 1;

    heap_get_stats(&h0);
    for (uint32_t i = 0; i < 8u; i++)
    {
        blocks[i] = kmalloc(100u + i * 8u);
        if (blocks[i] == NULL)
            heap_ok = 0;
        else
            memset(blocks[i], (int)(0xA0u + i), 100u + i * 8u);
    }
    for (uint32_t i = 0; i < 8u; i++)
    {
        const uint8_t *b = blocks[i];

        for (uint32_t j = 0; b != NULL && j < 100u + i * 8u; j++)
            if (b[j] != (uint8_t)(0xA0u + i))
                heap_ok = 0;
    }
    for (uint32_t i = 0; i < 8u; i++)
        kfree(blocks[i]);
    heap_get_stats(&h1);
    check(3, "heap: kmalloc/kfree keep data intact and restore accounting",
          heap_ok && h1.used_blocks == h0.used_blocks && h1.used_bytes == h0.used_bytes &&
              h1.used_bytes + h1.free_bytes + h1.overhead_bytes == h1.total_bytes);

    uint8_t *dirty = kmalloc(64);

    if (dirty != NULL)
    {
        memset(dirty, 0xAA, 64);
        kfree(dirty);
    }

    uint8_t *zero = kcalloc(64, 1);
    int zero_ok = zero != NULL;

    for (uint32_t j = 0; zero != NULL && j < 64u; j++)
        if (zero[j] != 0)
            zero_ok = 0;
    kfree(zero);
    check(3, "heap: kcalloc returns zeroed memory", zero_ok);

    heap_stats_t before, during, after;
    void *head = NULL;
    uint32_t n = 0;

    heap_get_stats(&before);
    for (;;)
    {
        void *p = kmalloc(256u * 1024u);

        if (p == NULL)
            break;
        *(void **)p = head; // rantai blok di dalam blok itu sendiri
        head = p;
        n++;
    }
    heap_get_stats(&during);
    while (head != NULL)
    {
        void *next = *(void **)head;

        kfree(head);
        head = next;
    }

    void *probe = kmalloc(64);
    int probe_ok = probe != NULL;

    kfree(probe);
    heap_get_stats(&after);
    check(3, "OOM: kmalloc returns NULL without crashing, heap then recovers",
          n >= 1u && during.failed_count > before.failed_count && probe_ok &&
              after.used_bytes == before.used_bytes);
}

/* ---------- R4: thread dan scheduler ---------- */

static void test_threads(void)
{
    sched_policy_t old_policy = scheduler_get_policy();
    uint32_t switches0 = scheduler_get_switches();

    scheduler_set_policy(SCHED_ROUND_ROBIN);

    /* siklus hidup: NEW -> READY -> RUNNING -> TERMINATED */
    thread_t *life = thread_create("st-life", spin_task, NULL, 5);
    int lifecycle = 0;

    if (life != NULL)
    {
        life->cpu_limit = 3; // berakhir sendiri setelah 3 tick CPU
        int was_new = life->state == THREAD_NEW;

        thread_start(life);
        int was_ready = life->state == THREAD_READY || life->state == THREAD_RUNNING;

        lifecycle = was_new && was_ready && wait_state(life, THREAD_TERMINATED, 2000);
    }
    check(4, "thread lifecycle NEW -> READY -> RUNNING -> TERMINATED", lifecycle);
    kill_both(life, NULL);

    /* preemption nyata: dua thread CPU-bound yang TIDAK pernah yield */
    cnt_a = 0;
    cnt_b = 0;

    thread_t *a = thread_create("st-rr-a", counter_task, (void *)&cnt_a, 5);
    thread_t *b = thread_create("st-rr-b", counter_task, (void *)&cnt_b, 5);
    int progress = 0, fair = 0;

    if (a != NULL && b != NULL)
    {
        thread_start(a);
        thread_start(b);
        thread_sleep_ms(400);

        uint32_t ta = a->cpu_ticks, tb = b->cpu_ticks;
        uint32_t diff = (ta > tb) ? ta - tb : tb - ta;

        progress = cnt_a > 0 && cnt_b > 0;
        fair = ta >= 10u && tb >= 10u && diff <= 10u;
    }
    check(4, "preemption: two CPU-bound threads that never yield both progress", progress);
    check(4, "Round Robin shares the CPU (tick difference <= 10)", fair);
    kill_both(a, b);

    /* priority: prioritas 7 menang atas 3 (di bawah prioritas shell = 8, agar shell tidak lapar) */
    cnt_a = 0;
    cnt_b = 0;
    scheduler_set_policy(SCHED_PRIORITY);

    thread_t *hi = thread_create("st-hi", counter_task, (void *)&cnt_a, 7);
    thread_t *lo = thread_create("st-lo", counter_task, (void *)&cnt_b, 3);
    int prio_ok = 0;

    if (hi != NULL && lo != NULL)
    {
        thread_start(hi);
        thread_start(lo);
        thread_sleep_ms(300);
        prio_ok = hi->cpu_ticks >= 20u && lo->cpu_ticks <= 2u && cnt_a > cnt_b;
    }
    scheduler_set_policy(old_policy);
    check(4, "Priority policy favors the higher-priority thread", prio_ok);
    kill_both(hi, lo);

    check(4, "context switches occurred (>= 8)", scheduler_get_switches() - switches0 >= 8u);
    check(4, "idle thread exists", thread_idle() != NULL);
}

/* ---------- R5: sinkronisasi ---------- */

static void test_sync(void)
{
    static mutex_t m;
    static semaphore_t s;
    race_result_t r;
    int reproduced = 0;

    static const uint32_t attempts[3] = {100000u, 500000u, 2500000u};
    for (int attempt = 0; attempt < 3 && !reproduced; attempt++)
        if (race_run(RACE_NONE, attempts[attempt], &r) == 0 && r.lost > 0)
            reproduced = 1;
    check(5, "race condition reproduced without synchronization", reproduced);

    int mutex_ok = race_run(RACE_MUTEX, 100000u, &r) == 0 && r.lost == 0 && r.actual == r.expected;

    check(5, "mutex keeps the shared counter exact (no lost updates)", mutex_ok);

    int sem_ok = race_run(RACE_SEM, 100000u, &r) == 0 && r.lost == 0 && r.actual == r.expected;

    check(5, "binary semaphore keeps the shared counter exact", sem_ok);

    mutex_init(&m, "st-mutex", 90);
    int owner_ok = mutex_trylock(&m) == 0 && mutex_trylock(&m) != 0;

    mutex_unlock(&m);
    owner_ok = owner_ok && mutex_trylock(&m) == 0;
    mutex_unlock(&m);
    check(5, "mutex: exclusive ownership, release, re-acquire", owner_ok);

    sem_init(&s, "st-sem", 2);
    sem_wait(&s);
    sem_wait(&s);
    int drained = s.count == 0;

    sem_post(&s);
    check(5, "semaphore counting: wait, wait, post leaves count = 1", drained && s.count == 1);
}

/* ---------- R6: deadlock ---------- */

typedef struct
{
    mutex_t *first;
    mutex_t *second;
} lock_pair_t;

static void dl_finish(void)
{
    uint32_t flags = irq_save();

    dl_finished++;
    irq_restore(flags);
}

static void dl_task_ab(void *arg)
{
    (void)arg;
    mutex_lock(&da);
    thread_sleep_ms(80);
    mutex_lock(&db);
    mutex_unlock(&db);
    mutex_unlock(&da);
    dl_finish();
}

static void dl_task_ba(void *arg)
{
    (void)arg;
    mutex_lock(&db);
    thread_sleep_ms(80);
    mutex_lock(&da);
    mutex_unlock(&da);
    mutex_unlock(&db);
    dl_finish();
}

static void dl_task_ordered(void *arg)
{
    lock_pair_t *p = arg;

    mutex_lock_pair(p->first, p->second); // selalu terkunci menurut rank
    thread_sleep_ms(80);
    mutex_unlock_pair(p->first, p->second);
    dl_finish();
}

static void test_deadlock(void)
{
    static lock_pair_t p1, p2;
    int detected = 0, recovered = 0, ordered = 0;

    mutex_init(&da, "st-da", 91);
    mutex_init(&db, "st-db", 92);
    dl_finished = 0;

    thread_t *t1 = thread_create("st-dl-ab", dl_task_ab, NULL, 5);
    thread_t *t2 = thread_create("st-dl-ba", dl_task_ba, NULL, 5);

    if (t1 != NULL && t2 != NULL)
    {
        thread_start(t1);
        thread_start(t2);
        thread_sleep_ms(400);
        detected = t1->state == THREAD_WAITING && t2->state == THREAD_WAITING &&
                   sync_detect_deadlock() >= 1u;

        thread_kill(t1->id); // korban: lepas 'da', diserahkan ke t2
        wait_state(t2, THREAD_TERMINATED, 1000);
        recovered = t2->state == THREAD_TERMINATED && dl_finished == 1u &&
                    !da.locked && !db.locked && sync_detect_deadlock() == 0u;
    }
    kill_both(t1, t2);
    check(6, "deadlock reproduced: A->B and B->A wait on each other", detected);
    check(6, "detector found the cycle; killing one victim recovers the other", recovered);

    dl_finished = 0;
    p1.first = &da;
    p1.second = &db;
    p2.first = &db;
    p2.second = &da;

    thread_t *o1 = thread_create("st-ord-1", dl_task_ordered, &p1, 5);
    thread_t *o2 = thread_create("st-ord-2", dl_task_ordered, &p2, 5);

    if (o1 != NULL && o2 != NULL)
    {
        thread_start(o1);
        thread_start(o2);
        wait_state(o1, THREAD_TERMINATED, 2000);
        wait_state(o2, THREAD_TERMINATED, 2000);
        ordered = o1->state == THREAD_TERMINATED && o2->state == THREAD_TERMINATED &&
                  dl_finished == 2u && sync_detect_deadlock() == 0u;
    }
    kill_both(o1, o2);
    check(6, "lock ordering (mutex_lock_pair) prevents circular wait", ordered);
}

/* ---------- R7: user mode dan proteksi ---------- */

static int run_user(const char *program, uint32_t *pid_out, proc_state_t *state,
                    int32_t *code, uint32_t *vec)
{
    int pid = process_spawn(program);
    uint32_t waited = 0;

    if (pid <= 0)
        return -1;
    *pid_out = (uint32_t)pid;
    while (process_is_running((uint32_t)pid) && waited < 3000u)
    {
        thread_sleep_ms(20);
        waited += 20u;
    }
    return process_get_result((uint32_t)pid, state, code, vec);
}

static void test_user_mode(void)
{
    uint32_t pid = 0, vec = 0, tr;
    int32_t code = 0;
    proc_state_t st = PROC_UNUSED;
    int ok;

    __asm__ volatile("str %0" : "=r"(tr));
    tr &= 0xFFFFu;
    check(7, "TSS loaded (TR = 0x28) for Ring 3 -> Ring 0 transitions", tr == TSS_SEL);
    check(7, "kernel memory is not user-accessible (page tables)",
          paging_user_range_ok(0x00100000u, 4, 0) == 0);

    ok = run_user("hello", &pid, &st, &code, &vec) == 0;
    check(7, "Ring 3 program runs; allowed syscalls (write, getpid, exit) succeed",
          ok && st == PROC_EXITED && code == (int32_t)pid);

    ok = run_user("badptr", &pid, &st, &code, &vec) == 0;
    check(7, "syscall with a kernel pointer is rejected; process survives",
          ok && st == PROC_EXITED && code == 0);

    ok = run_user("kwrite", &pid, &st, &code, &vec) == 0;
    check(7, "user write to kernel memory -> page fault, process killed",
          ok && st == PROC_FAULTED && vec == 14u);

    ok = run_user("rowrite", &pid, &st, &code, &vec) == 0;
    check(7, "user write to read-only code page -> page fault, process killed",
          ok && st == PROC_FAULTED && vec == 14u);

    ok = run_user("priv", &pid, &st, &code, &vec) == 0;
    check(7, "privileged instruction in Ring 3 -> #GP, process killed",
          ok && st == PROC_FAULTED && vec == 13u);

    thread_reap();
    run_user("hello", &pid, &st, &code, &vec); // pemanasan: page table + heap
    thread_reap();

    size_t free0 = pmm_get_free_pages();

    for (int i = 0; i < 3; i++)
    {
        run_user("hello", &pid, &st, &code, &vec);
        run_user("kwrite", &pid, &st, &code, &vec);
        thread_reap();
    }
    check(7, "process pages returned to the PMM (no leak after exit and fault)",
          pmm_get_free_pages() == free0);
    check(7, "fault paths executed (exception counters #13 and #14)",
          stats_get_exception(13) >= 1u && stats_get_exception(14) >= 2u);
}

/* ---------- R8: filesystem ---------- */

typedef struct
{
    char tag;
    uint32_t count;
    volatile uint32_t done;
    volatile uint32_t errors;
} fs_job_t;

static fs_job_t job_a, job_b;

static void fs_worker(void *arg)
{
    fs_job_t *job = arg;
    char path[] = "/stress/x00";

    for (uint32_t i = 0; i < job->count; i++)
    {
        path[8] = job->tag;
        path[9] = (char)('0' + i / 10u);
        path[10] = (char)('0' + i % 10u);
        if (vfs_write_file(path, "stress", 6, 0) != VFS_OK)
            job->errors++;
    }
    job->done = 1;
}

static void test_filesystem(void)
{
    uint32_t pass = 0, fail = 0, problems = 1;
    vfs_stat_t st;
    vfs_dirent_t e;
    char path[] = "/stress/x00";

    int rc = fstest_run(&pass, &fail);

    check(8, "CRUD suite on RAMDisk/VFS/SeventhFS passes (create, read, update, delete)",
          rc == 0 && pass >= 12u && fail == 0);
    check(8, "boot-time file /README.txt exists", vfs_stat("/README.txt", &st) == VFS_OK && st.size > 0);

    int made = vfs_mkdir("/stress") == VFS_OK;

    job_a.tag = 'a';
    job_a.count = 40;
    job_a.done = 0;
    job_a.errors = 0;
    job_b.tag = 'b';
    job_b.count = 40;
    job_b.done = 0;
    job_b.errors = 0;

    thread_t *wa = thread_create("st-fs-a", fs_worker, &job_a, 5);
    thread_t *wb = thread_create("st-fs-b", fs_worker, &job_b, 5);

    if (wa != NULL && wb != NULL)
    {
        thread_start(wa);
        thread_start(wb);
        for (uint32_t waited = 0; waited < 5000u && !(job_a.done && job_b.done); waited += 20u)
            thread_sleep_ms(20);
    }

    uint32_t entries = 0;

    while (vfs_readdir("/stress", entries, &e) == 0)
        entries++;
    check(8, "two threads write 80 files concurrently; VFS mutex serializes access",
          made && job_a.done && job_b.done && job_a.errors == 0 && job_b.errors == 0 && entries == 80u);
    kill_both(wa, wb);

    for (uint32_t t = 0; t < 2u; t++)
    {
        for (uint32_t i = 0; i < 40u; i++)
        {
            path[8] = (t == 0) ? 'a' : 'b';
            path[9] = (char)('0' + i / 10u);
            path[10] = (char)('0' + i % 10u);
            vfs_remove(path);
        }
    }
    vfs_rmdir("/stress");
    check(8, "fsck after concurrent access and cleanup: 0 problems",
          vfs_check(&problems) == VFS_OK && problems == 0);
}

/* ---------- pemeriksaan akhir dan ringkasan ---------- */

static void final_checks(uint32_t threads_before)
{
    heap_stats_t h;

    thread_reap();
    heap_get_stats(&h);
    check(3, "heap accounting invariant holds after all tests",
          h.used_bytes + h.free_bytes + h.overhead_bytes == h.total_bytes);
    check(4, "no leaked threads after all tests", thread_count() == threads_before);
    check(6, "no circular wait left behind", sync_detect_deadlock() == 0u);
}

uint32_t selftest_run(int verbose_flag)
{
    uint32_t threads_before = thread_count();
    uint32_t pass = 0, fail = 0;

    verbose = verbose_flag;
    memset(req_pass, 0, sizeof(req_pass));
    memset(req_fail, 0, sizeof(req_fail));

    terminal_set_color(COLOR_YELLOW, COLOR_BLACK);
    terminal_write("=== SeventhOS selftest ===\n");
    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);

    test_boot();
    test_interrupts();
    test_memory();
    test_threads();
    test_sync();
    test_deadlock();
    test_user_mode();
    test_filesystem();
    final_checks(threads_before);

    terminal_set_color(COLOR_YELLOW, COLOR_BLACK);
    terminal_write("=== SELFTEST SUMMARY ===\n");
    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    for (uint32_t i = 0; i < REQ_COUNT; i++)
    {
        terminal_write("R");
        terminal_write_dec(i + 1u);
        terminal_write(" ");
        terminal_write_pad(req_names[i], 52);
        terminal_write_dec(req_pass[i]);
        terminal_write("/");
        terminal_write_dec(req_pass[i] + req_fail[i]);
        terminal_write(req_fail[i] == 0 ? "\n" : "  <-- FAILED\n");
        pass += req_pass[i];
        fail += req_fail[i];
    }

    terminal_set_color(fail == 0 ? COLOR_LIGHT_GREEN : COLOR_LIGHT_RED, COLOR_BLACK);
    terminal_write(fail == 0 ? "SELFTEST RESULT: PASS (" : "SELFTEST RESULT: FAIL (");
    terminal_write_dec(pass);
    terminal_write("/");
    terminal_write_dec(pass + fail);
    terminal_write(" checks)\n");
    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    return fail;
}

void selftest_exit(int success)
{
    outb(0xF4, success ? 0x10 : 0x11);
}