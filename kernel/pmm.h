
#ifndef SEVENTHOS_PMM_H
#define SEVENTHOS_PMM_H

#include <stdint.h>
#include <stddef.h>

#define PMM_PAGE_SIZE 4096u

void pmm_initialize(uint32_t multiboot_info_addr);

uintptr_t pmm_alloc_page(void);
void pmm_free_page(uintptr_t address);

size_t pmm_get_total_pages(void);
size_t pmm_get_free_pages(void);
size_t pmm_get_used_pages(void);

#endif
