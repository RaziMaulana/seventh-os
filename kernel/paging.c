#include <stddef.h>
#include <stdint.h>

#include "kstring.h"
#include "paging.h"
#include "pmm.h"
#include "terminal.h"
#include "process.h"

#define ADDR_MASK 0xFFFFF000u
#define CR0_PG 0x80000000u
#define CR0_WP 0x00010000u // kernel juga menghormati bit read-only (bekal Fase 4)

static uint32_t *page_directory;
static uint32_t identity_limit;
static int paging_on;

static inline void invlpg(uint32_t addr)
{
    __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

// Ambil 1 halaman fisik dan nolkan. Aman diakses langsung karena identity map.
static uint32_t *alloc_table(void)
{
    uintptr_t phys = pmm_alloc_page();
    if (phys == 0)
        return NULL;
    memset((void *)phys, 0, PMM_PAGE_SIZE);
    return (uint32_t *)phys;
}

int paging_initialize(void)
{
    uint32_t limit = (uint32_t)pmm_get_highest_address();

    if (limit < 0x00400000u)
        return -1; // PMM tidak melaporkan RAM yang cukup: jangan diam-diam memetakan 4 MiB
    if (limit > PMM_MANAGED_LIMIT)
        limit = PMM_MANAGED_LIMIT;
    limit &= ~(PMM_PAGE_SIZE - 1u);

    page_directory = alloc_table();
    if (page_directory == NULL)
        return -1;

    uint32_t tables = (limit + 0x3FFFFFu) >> 22; // 1 tabel = 4 MiB

    for (uint32_t t = 0; t < tables; t++)
    {
        uint32_t *pt = alloc_table();
        if (pt == NULL)
            return -1;

        for (uint32_t i = 0; i < 1024u; i++)
        {
            uint32_t addr = (t << 22) | (i << 12);
            pt[i] = (addr < limit) ? (addr | PAGE_PRESENT | PAGE_WRITE) : 0;
        }
        page_directory[t] = (uint32_t)(uintptr_t)pt | PAGE_PRESENT | PAGE_WRITE;
    }

    identity_limit = limit;

    uint32_t cr0;
    __asm__ volatile("mov %0, %%cr3" : : "r"((uint32_t)(uintptr_t)page_directory) : "memory");
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= CR0_PG | CR0_WP;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");

    paging_on = 1;
    return 0;
}

int paging_is_enabled(void) { return paging_on; }
uint32_t paging_get_identity_limit(void) { return identity_limit; }

int paging_map_page(uint32_t virt, uint32_t phys, uint32_t flags)
{
    if ((virt & 0xFFFu) || (phys & 0xFFFu) || page_directory == NULL)
        return -1;

    uint32_t pd_index = virt >> 22;
    uint32_t pt_index = (virt >> 12) & 0x3FFu;
    uint32_t *pt;

    if (!(page_directory[pd_index] & PAGE_PRESENT))
    {
        pt = alloc_table(); // page table dibuat saat dibutuhkan
        if (pt == NULL)
            return -1;
        page_directory[pd_index] = (uint32_t)(uintptr_t)pt | PAGE_PRESENT | PAGE_WRITE | (flags & PAGE_USER);
    }
    else
    {
        pt = (uint32_t *)(uintptr_t)(page_directory[pd_index] & ADDR_MASK);
        if (flags & PAGE_USER)
            page_directory[pd_index] |= PAGE_USER; // izin akhir tetap ditentukan PTE
    }

    pt[pt_index] = (phys & ADDR_MASK) | (flags & 0xFFFu) | PAGE_PRESENT;
    invlpg(virt); // buang entri TLB lama
    return 0;
}

uint32_t paging_unmap_page(uint32_t virt)
{
    uint32_t pde = page_directory[virt >> 22];
    if (!(pde & PAGE_PRESENT))
        return 0;

    uint32_t *pt = (uint32_t *)(uintptr_t)(pde & ADDR_MASK);
    uint32_t pte = pt[(virt >> 12) & 0x3FFu];
    if (!(pte & PAGE_PRESENT))
        return 0;

    pt[(virt >> 12) & 0x3FFu] = 0;
    invlpg(virt);
    return pte & ADDR_MASK; // 0 aman sebagai "tidak ada": PMM tak pernah memberi halaman 0
}

int paging_translate(uint32_t virt, uint32_t *phys_out)
{
    uint32_t pde = page_directory[virt >> 22];
    if (!(pde & PAGE_PRESENT))
        return -1;

    uint32_t *pt = (uint32_t *)(uintptr_t)(pde & ADDR_MASK);
    uint32_t pte = pt[(virt >> 12) & 0x3FFu];
    if (!(pte & PAGE_PRESENT))
        return -1;

    if (phys_out)
        *phys_out = (pte & ADDR_MASK) | (virt & 0xFFFu);
    return 0;
}

int paging_user_range_ok(uint32_t virt, uint32_t length, int need_write)
{
    if (length == 0)
        return 1;

    uint32_t end = virt + length;
    if (end < virt)
        return 0; // overflow

    uint32_t need = PAGE_PRESENT | PAGE_USER | (need_write ? PAGE_WRITE : 0u);

    for (uint32_t pg = virt >> 12; pg <= (end - 1u) >> 12; pg++)
    {
        uint32_t pde = page_directory[pg >> 10];

        if ((pde & (PAGE_PRESENT | PAGE_USER)) != (PAGE_PRESENT | PAGE_USER))
            return 0;

        uint32_t *pt = (uint32_t *)(uintptr_t)(pde & ADDR_MASK);
        if ((pt[pg & 0x3FFu] & need) != need)
            return 0;
    }
    return 1;
}

void paging_page_fault_handler(isr_frame_t *f)
{
    uint32_t fault_addr;
    __asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));

    if ((f->cs & 3u) == 3u)
        process_handle_fault(f, "page fault", fault_addr);

    uint32_t error_code = f->error_code;

    terminal_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
    terminal_write("\n*** PAGE FAULT ***\nAddress: ");
    terminal_write_hex(fault_addr);
    terminal_write("\nEIP    : ");
    terminal_write_hex(f->eip);
    terminal_write("\nCause  : ");
    terminal_write((error_code & 1u) ? "protection violation" : "page not present");
    terminal_write((error_code & 2u) ? ", write" : ", read");
    terminal_write((error_code & 4u) ? ", user mode" : ", kernel mode");
    terminal_write("\nKernel halted.\n");

    for (;;)
        __asm__ volatile("cli; hlt");
}