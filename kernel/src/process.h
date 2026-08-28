#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>

typedef enum {
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_BLOCKED
} process_state_t;

struct process {
    uint64_t pid;                 // unique process ID
    process_state_t state;        // current scheduling state

    // Saved CPU register state — captured when this process is paused,
    // restored when it's resumed. Same idea as isr_common_stub's save/restore,
    // just persisted in this struct instead of only living on the stack.
    uint64_t rsp;                 // saved stack pointer (everything else lives ON that stack)

    uint64_t pml4_phys;           // this process's own page table (its address space)

    struct process *next;         // simple linked list of all processes, for the scheduler
};

void process_init(void);
struct process *process_create(void (*entry_point)(void));
void scheduler_run_next(void);

extern void context_switch(uint64_t *old_rsp, uint64_t new_rsp);

#endif
