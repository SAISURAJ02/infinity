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
    CAP_DRAW_REGION,   // permission to draw pixels within a specific screen region
} capability_type_t;

struct capability {
    capability_type_t type;
    uint32_t x_min, x_max, y_min, y_max; // for CAP_DRAW_REGION: the allowed region
};

struct process {
    uint64_t pid;                 // unique process ID
    process_state_t state;        // current scheduling state

    struct capability capabilities[MAX_CAPABILITIES];
    int capability_count;

    // Saved CPU register state — captured when this process is paused,
    // restored when it's resumed. Same idea as isr_common_stub's save/restore,
    // just persisted in this struct instead of only living on the stack.
    uint64_t rsp;                 // saved stack pointer (everything else lives ON that stack)

    uint64_t pml4_phys;           // this process's own page table (its address space)

    struct process *next;         // simple linked list of all processes, for the scheduler
};

uint64_t schedule(uint64_t current_rsp);

void process_init(void);
struct process *process_create(void (*entry_point)(void));
void scheduler_run_next(void);

int process_grant_capability(struct process *proc, capability_type_t type,
                              uint32_t x_min, uint32_t x_max,
                              uint32_t y_min, uint32_t y_max);
struct process *process_get_current(void);

extern void context_switch(uint64_t *old_rsp, uint64_t new_rsp);
extern void load_cr3(uint64_t pml4_phys_addr);
int process_check_capability(struct process *proc, capability_type_t type, uint32_t x, uint32_t y);

#endif
