#include "tss.h"
#include <stddef.h>

struct tss g_tss;

void tss_init(uint64_t rsp0) {
    uint8_t *p = (uint8_t *)&g_tss;
    for (size_t i = 0; i < sizeof(g_tss); i++) p[i] = 0;

    g_tss.rsp0 = rsp0;
    g_tss.iomap_base = sizeof(struct tss);  // no I/O bitmap -> ring 3 in/out always traps
}

void tss_set_rsp0(uint64_t rsp0) {
    g_tss.rsp0 = rsp0;
}
