#ifndef SEVENTHOS_PAGING_H
#define SEVENTHOS_PAGING_H

#include <stdint.h>
#include "idt.h"

#define PAGE_PRESENT 0x001u
#define PAGE_WRITE 0x002u
#define PAGE_USER 0x004u

int paging_initialize(void); 
int paging_is_enabled(void);
uint32_t paging_get_identity_limit(void);

int paging_map_page(uint32_t virt, uint32_t phys, uint32_t flags); 
uint32_t paging_unmap_page(uint32_t virt);                        
int paging_translate(uint32_t virt, uint32_t *phys_out);          

int paging_user_range_ok(uint32_t base, uint32_t size, int write);

void paging_page_fault_handler(isr_frame_t *frame) __attribute__((noreturn));

#endif