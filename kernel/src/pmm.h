#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>

void pmm_init(void);
void *pmm_alloc_frame(void);
void pmm_free_frame(void *addr);

#endif
