#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>

typedef enum {
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_BLOCKED
} process_state_t;

#define MAX_CAPABILITIES 8

typedef enum {
    CAP_NONE = 0,
    CAP_DRAW_REGION,
} capability_type_t;

struct capability {
    capability_type_t type;
    uint32_t x_min, x_max, y_min, y_max;
};

struct process {
    uint64_t pid;
    process_state_t state;

    struct capability capabilities[MAX_CAPABILITIES];
    int capability_count;

    uint64_t rsp;         // kernel-mode continuation pointer — same meaning as today
    uint64_t pml4_phys;
    uint64_t kstack_top;  // top of this process's private ring-0 stack (for future TSS.rsp0 sync, Pillar 5)
    uint64_t ustack_top;  // top of this process's ring-3 user stack — 0 for ring-0-only processes

    struct process *next;
};

uint64_t schedule(uint64_t current_rsp);

void process_init(void);
struct process *process_create(void (*entry_point)(void));
void scheduler_run_next(void);

int process_grant_capability(struct process *proc, capability_type_t type,
                              uint32_t x_min, uint32_t x_max,
                              uint32_t y_min, uint32_t y_max);
struct process *process_get_current(void);
struct process *process_get_list(void);
struct process *process_create(void (*entry_point)(void));
struct process *process_create_user(void);

extern void context_switch(uint64_t *old_rsp, uint64_t new_rsp);
extern void load_cr3(uint64_t pml4_phys_addr);
int process_check_capability(struct process *proc, capability_type_t type, uint32_t x, uint32_t y);

#endif
