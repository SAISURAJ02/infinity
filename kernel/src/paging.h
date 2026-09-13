#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

#define PAGE_PRESENT  (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)
#define PAGE_USER     (1ULL << 2)

void paging_init(uint64_t fb_virt_addr, uint64_t fb_phys_addr, uint64_t fb_size);
void paging_map(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);
void paging_map_into(uint64_t target_pml4_phys, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);
uint64_t paging_virt_to_phys_hhdm(uint64_t virt_addr);
uint64_t paging_phys_to_virt_hhdm(uint64_t phys_addr);
uint64_t paging_get_pml4(void);
extern void paging_switch(uint64_t pml4_phys_addr, uint64_t new_stack_top, void (*continuation)(void));

#endif
