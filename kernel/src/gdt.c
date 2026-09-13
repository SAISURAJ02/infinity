#include "gdt.h"
#include "tss.h"

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct tss_descriptor {
    uint16_t length;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  flags1;
    uint8_t  flags2;
    uint8_t  base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));

static struct gdt_entry gdt[7];   // 5 code/data slots + 2 slots for the 16-byte TSS descriptor
static struct gdt_ptr   gdtp;

extern void gdt_flush(uint64_t);
extern void tss_flush(void);

static void gdt_set_entry(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[i].base_low    = (base & 0xFFFF);
    gdt[i].base_mid    = (base >> 16) & 0xFF;
    gdt[i].base_high   = (base >> 24) & 0xFF;
    gdt[i].limit_low   = (limit & 0xFFFF);
    gdt[i].granularity = (limit >> 16) & 0x0F;
    gdt[i].granularity |= gran & 0xF0;
    gdt[i].access      = access;
}

static void gdt_set_tss(int idx, uint64_t base, uint32_t limit) {
    struct tss_descriptor *td = (struct tss_descriptor *)&gdt[idx];
    td->length     = limit & 0xFFFF;
    td->base_low   = base & 0xFFFF;
    td->base_mid   = (base >> 16) & 0xFF;
    td->flags1     = 0x89;              // present, DPL=0, type=1001 (64-bit TSS, available)
    td->flags2     = (limit >> 16) & 0x0F;
    td->base_high  = (base >> 24) & 0xFF;
    td->base_upper = (uint32_t)(base >> 32);
    td->reserved   = 0;
}

void gdt_init(void) {
    gdtp.limit = (sizeof(struct gdt_entry) * 7) - 1;   // 55 — total size in bytes, minus 1
    gdtp.base  = (uint64_t)&gdt;

    gdt_set_entry(0, 0, 0, 0,    0);      // null
    gdt_set_entry(1, 0, 0, 0x9A, 0xA0);   // kernel code
    gdt_set_entry(2, 0, 0, 0x92, 0xA0);   // kernel data
    gdt_set_entry(3, 0, 0, 0xFA, 0xA0);   // user code, DPL=3
    gdt_set_entry(4, 0, 0, 0xF2, 0xA0);   // user data, DPL=3

    tss_init(0xFFFFFFFFA0000000ULL);          // placeholder rsp0 — today's one shared kernel stack.
                                               // Pillar 5 will overwrite this per-process.
    gdt_set_tss(5, (uint64_t)&g_tss, sizeof(struct tss) - 1);  // occupies indices 5 AND 6

    gdt_flush((uint64_t)&gdtp);
    tss_flush();
}
