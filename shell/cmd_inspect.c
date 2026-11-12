#include <stddef.h>
#include <stdint.h>

#include "cmd_inspect.h"
#include "cpu.h"
#include "heap.h"
#include "paging.h"
#include "pmm.h"
#include "scheduler.h"
#include "shell_util.h"
#include "stats.h"
#include "sync.h"
#include "terminal.h"
#include "thread.h"
#include "timer.h"
#include "vfs.h"
#include "version.h"

static void label(const char *name)
{
    terminal_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    terminal_write_pad(name, 11);
    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    terminal_write(": ");
}

static void print_seconds(uint32_t ms)
{
    terminal_write_dec(ms / 1000u);
    terminal_putchar('.');
    if (ms % 1000u < 100u)
        terminal_putchar('0');
    if (ms % 1000u < 10u)
        terminal_putchar('0');
    terminal_write_dec(ms % 1000u);
}

/* ---------- sysinfo: ringkasan satu layar ---------- */

void cmd_sysinfo(void)
{
    heap_stats_t h;
    vfs_info_t fi;
    uint32_t count[5] = {0, 0, 0, 0, 0};
    uint32_t user_procs = 0;

    heap_get_stats(&h);
    vfs_info(&fi);

    uint32_t flags = irq_save();
    for (thread_t *t = thread_first(); t != NULL; t = t->next)
    {
        count[t->state]++;
        if (t->process != NULL && t->state != THREAD_TERMINATED)
            user_procs++;
    }
    irq_restore(flags);

    uint32_t total_kib = (uint32_t)(pmm_get_total_pages() * 4u);
    uint32_t used_kib = (uint32_t)(pmm_get_used_pages() * 4u);
    uint32_t mem_pct = (total_kib != 0) ? used_kib * 100u / total_kib : 0;

    terminal_set_color(COLOR_YELLOW, COLOR_BLACK);
    terminal_write("SeventhOS v" SEVENTHOS_VERSION);
    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    terminal_write("  x86 32-bit, GRUB Multiboot, preemptive kernel\n");

    label("Uptime");
    print_seconds(timer_get_uptime_ms());
    terminal_write(" s  (PIT ");
    terminal_write_dec(timer_get_frequency());
    terminal_write(" Hz, ");
    terminal_write_dec(timer_get_ticks());
    terminal_write(" ticks)\n");

    label("Memory");
    print_bar(mem_pct);
    terminal_write(" ");
    terminal_write_dec(mem_pct);
    terminal_write("% used, ");
    terminal_write_dec((total_kib - used_kib) / 1024u);
    terminal_write(" of ");
    terminal_write_dec(total_kib / 1024u);
    terminal_write(" MiB free\n");

    label("Heap");
    terminal_write_dec(h.total_bytes / 1024u);
    terminal_write(" KiB mapped, ");
    terminal_write_dec(h.used_bytes);
    terminal_write(" B used in ");
    terminal_write_dec(h.used_blocks);
    terminal_write(" blocks, allocs=");
    terminal_write_dec(h.alloc_count);
    terminal_write(" failed=");
    terminal_write_dec(h.failed_count);
    terminal_write("\n");

    label("Paging");
    terminal_write(paging_is_enabled() ? "ENABLED" : "DISABLED");
    terminal_write(", identity map 0 - ");
    terminal_write_dec(paging_get_identity_limit() / (1024u * 1024u));
    terminal_write(" MiB\n");

    label("Threads");
    terminal_write_dec(count[THREAD_NEW] + count[THREAD_READY] + count[THREAD_RUNNING] +
                       count[THREAD_WAITING]);
    terminal_write(" alive (running ");
    terminal_write_dec(count[THREAD_RUNNING]);
    terminal_write(", ready ");
    terminal_write_dec(count[THREAD_READY]);
    terminal_write(", waiting ");
    terminal_write_dec(count[THREAD_WAITING]);
    terminal_write("), user processes ");
    terminal_write_dec(user_procs);
    terminal_write("\n");

    label("Scheduler");
    terminal_write(scheduler_policy_name());
    terminal_write(", quantum ");
    terminal_write_dec(THREAD_DEFAULT_SLICE);
    terminal_write(" ticks, context switches ");
    terminal_write_dec(scheduler_get_switches());
    terminal_write("\n");

    label("Interrupts");
    terminal_write("IRQ0=");
    terminal_write_dec(stats_get_irq(0));
    terminal_write(" IRQ1=");
    terminal_write_dec(stats_get_irq(1));
    terminal_write(" syscalls=");
    terminal_write_dec(stats_total_syscalls());
    terminal_write(" exceptions=");
    terminal_write_dec(stats_total_exceptions());
    terminal_write("\n");

    label("Filesystem");
    if (fi.fs_name == NULL)
    {
        terminal_write("not mounted\n");
    }
    else
    {
        terminal_write(fi.fs_name);
        terminal_write(" on ");
        terminal_write(fi.device);
        terminal_write(": blocks ");
        terminal_write_dec(fi.total_blocks - fi.free_blocks);
        terminal_write("/");
        terminal_write_dec(fi.total_blocks);
        terminal_write(", inodes ");
        terminal_write_dec(fi.total_inodes - fi.free_inodes);
        terminal_write("/");
        terminal_write_dec(fi.total_inodes);
        terminal_write("\n");
    }
}

/* ---------- top: pemakaian CPU per thread dalam jendela 1 detik ---------- */

#define TOP_MAX 24u

void cmd_top(void)
{
    uint32_t id0[TOP_MAX], cpu0[TOP_MAX], n = 0;
    uint32_t flags = irq_save();

    for (thread_t *t = thread_first(); t != NULL && n < TOP_MAX; t = t->next)
    {
        id0[n] = t->id;
        cpu0[n] = t->cpu_ticks;
        n++;
    }
    irq_restore(flags);

    uint32_t start = timer_get_ticks();

    terminal_write("Sampling CPU usage for 1 second...\n");
    thread_sleep_ms(1000);

    uint32_t window = timer_get_ticks() - start;
    uint32_t idle10 = 0;

    if (window == 0)
        window = 1;

    terminal_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    terminal_write("ID  NAME        STATE       PRI TOTAL   CPU(1s)\n");
    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);

    flags = irq_save();
    for (thread_t *t = thread_first(); t != NULL; t = t->next)
    {
        uint32_t base = 0;

        for (uint32_t j = 0; j < n; j++)
            if (id0[j] == t->id)
                base = cpu0[j]; // thread baru: baseline 0

        uint32_t pct10 = (t->cpu_ticks - base) * 1000u / window;
        if (pct10 > 1000u)
            pct10 = 1000u;
        if (t->is_idle)
            idle10 = pct10;

        terminal_write_dec_pad(t->id, 4);
        terminal_write_pad(t->name, 12);
        terminal_write_pad(thread_state_name(t->state), 12);
        terminal_write_dec_pad(t->priority, 4);
        terminal_write_dec_pad(t->cpu_ticks, 8);
        print_tenths(pct10);
        terminal_write("%\n");
    }
    irq_restore(flags);

    terminal_write("CPU busy ");
    print_tenths(1000u - idle10);
    terminal_write("%, idle ");
    print_tenths(idle10);
    terminal_write("%  (window ");
    terminal_write_dec(window);
    terminal_write(" ticks)\n");
}

/* ---------- irqinfo: monitor interrupt ---------- */

static void irq_row(const char *vec, const char *source, uint32_t total, uint32_t delta)
{
    terminal_write_pad(vec, 8);
    terminal_write_pad(source, 22);
    terminal_write_dec_pad(total, 10);
    terminal_write_dec(delta);
    terminal_write("\n");
}

void cmd_irqinfo(void)
{
    uint32_t t0 = timer_get_uptime_ms();
    uint32_t irq0 = stats_get_irq(0), irq1 = stats_get_irq(1);
    uint32_t sys0 = stats_total_syscalls(), exc0 = stats_total_exceptions();

    terminal_write("Sampling for 1 second (press keys to watch IRQ1)...\n");
    thread_sleep_ms(1000);

    uint32_t window = timer_get_uptime_ms() - t0;

    terminal_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    terminal_write("VECTOR  SOURCE                TOTAL     IN WINDOW\n");
    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    irq_row("0x20", "IRQ0 PIT timer", stats_get_irq(0), stats_get_irq(0) - irq0);
    irq_row("0x21", "IRQ1 keyboard", stats_get_irq(1), stats_get_irq(1) - irq1);
    irq_row("0x80", "int 0x80 syscall", stats_total_syscalls(), stats_total_syscalls() - sys0);
    irq_row("0-31", "CPU exceptions", stats_total_exceptions(), stats_total_exceptions() - exc0);

    terminal_write("Window: ");
    terminal_write_dec(window);
    terminal_write(" ms\nExceptions by vector:");

    int any = 0;
    for (uint32_t v = 0; v < STATS_VECTORS; v++)
    {
        if (stats_get_exception(v) != 0)
        {
            terminal_write(" #");
            terminal_write_dec(v);
            terminal_write("=");
            terminal_write_dec(stats_get_exception(v));
            any = 1;
        }
    }
    terminal_write(any ? "\n" : " none\n");

    uint8_t master = inb(0x21), slave = inb(0xA1);

    terminal_write("PIC mask: master=");
    terminal_write_hex(master);
    terminal_write(" slave=");
    terminal_write_hex(slave);
    terminal_write("  IRQ0 ");
    terminal_write((master & 1u) ? "masked" : "enabled");
    terminal_write(", IRQ1 ");
    terminal_write((master & 2u) ? "masked\n" : "enabled\n");
    terminal_write("IDT: 256 entries; gates = 32 exceptions, IRQ0, IRQ1, int 0x80 (DPL 3)\n");
}

void cmd_diag(void)
{
    static mutex_t probe;
    uint32_t pass = 0, fail = 0;
    uint32_t problems = 1;
    uint32_t cs;
    heap_stats_t h0, h1;

    terminal_write("Kernel self-diagnostics\n");

    /* PMM: alokasi dan bebaskan satu halaman, hitungan harus kembali */
    size_t free_before = pmm_get_free_pages();
    uintptr_t page = pmm_alloc_page();
    int pmm_ok = (page != 0) && (pmm_get_free_pages() == free_before - 1u);
    if (page != 0)
        pmm_free_page(page);
    pmm_ok = pmm_ok && (pmm_get_free_pages() == free_before);
    print_check("PMM: alloc/free round trip keeps accounting exact", pmm_ok, &pass, &fail);

    /* Heap: used + free + headers == total, dan kmalloc/kfree tidak mengubah jumlah blok terpakai */
    heap_get_stats(&h0);
    int heap_ok = (h0.used_bytes + h0.free_bytes + h0.overhead_bytes == h0.total_bytes);
    void *a = kmalloc(100);
    void *b = kmalloc(200);
    heap_ok = heap_ok && a != NULL && b != NULL;
    kfree(a);
    kfree(b);
    heap_get_stats(&h1);
    heap_ok = heap_ok && (h1.used_blocks == h0.used_blocks);
    print_check("heap: block accounting + kmalloc/kfree round trip", heap_ok, &pass, &fail);

    /* Paging dan proteksi */
    print_check("paging enabled, kernel pages are supervisor-only",
                paging_is_enabled() && paging_user_range_ok(0x00100000u, 4, 0) == 0, &pass, &fail);

    __asm__ volatile("mov %%cs, %0" : "=r"(cs));
    print_check("kernel executes in Ring 0 (CPL 0)", (cs & 3u) == 0, &pass, &fail);

    /* Timer + scheduler: tidur 30 ms harus ~3 tick dan memicu context switch */
    uint32_t t0 = timer_get_ticks();
    uint32_t sw0 = scheduler_get_switches();
    thread_sleep_ms(30);
    uint32_t dt = timer_get_ticks() - t0;
    print_check("PIT: ticks advance (30 ms sleep = 3..15 ticks)", dt >= 3u && dt <= 15u, &pass, &fail);
    print_check("scheduler: context switches happen", scheduler_get_switches() > sw0, &pass, &fail);
    print_check("IRQ0 handler is counting", stats_get_irq(0) > 0, &pass, &fail);

    /* Thread subsystem */
    print_check("idle thread exists and >= 2 threads alive",
                thread_idle() != NULL && thread_count() >= 2u, &pass, &fail);

    /* Mutex: lock, trylock kedua harus gagal, unlock, bisa dikunci lagi */
    mutex_init(&probe, "diag", 99);
    int mutex_ok = (mutex_trylock(&probe) == 0) && (mutex_trylock(&probe) != 0);
    mutex_unlock(&probe);
    mutex_ok = mutex_ok && (mutex_trylock(&probe) == 0);
    mutex_unlock(&probe);
    print_check("mutex: lock / trylock-fail / unlock / relock", mutex_ok, &pass, &fail);

    print_check("no circular wait (deadlock detector)", sync_detect_deadlock() == 0, &pass, &fail);

    print_check("filesystem consistency (fsck): 0 problems",
                vfs_check(&problems) == VFS_OK && problems == 0, &pass, &fail);

    terminal_write("Result: ");
    terminal_write_dec(pass);
    terminal_write(" passed, ");
    terminal_write_dec(fail);
    terminal_write(" failed\n");
}