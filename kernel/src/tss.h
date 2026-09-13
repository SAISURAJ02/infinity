#ifndef TSS_H
#define TSS_H

#include <stdint.h>

struct tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

extern struct tss g_tss;

void tss_init(uint64_t rsp0);
void tss_set_rsp0(uint64_t rsp0);   // Pillar 5 will call this every context switch

#endif
