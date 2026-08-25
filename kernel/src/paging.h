#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

#define PAGE_PRESENT  (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)

void paging_init(uint64_t fb_phys_addr, uint64_t fb_size);
void paging_map(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);
uint64_t paging_virt_to_phys_hhdm(uint64_t virt_addr);

#endif
