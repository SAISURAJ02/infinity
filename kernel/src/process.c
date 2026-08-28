#include "process.h"
#include "pmm.h"
#include "heap.h"
#include <stddef.h>

static uint64_t next_pid = 1;
static struct process *process_list = NULL;
static struct process *current_process = NULL;

#define PROCESS_STACK_SIZE (4 * 4096) // 16KB per process, same as our kernel stack

void process_init(void) {
    process_list = NULL;
    current_process = NULL;
    next_pid = 1;
}

struct process *process_create(void (*entry_point)(void)) {
    struct process *proc = (struct process *)kmalloc(sizeof(struct process));
    if (proc == NULL) {
        return NULL;
    }

    proc->pid = next_pid++;
    proc->state = PROCESS_READY;

    // Allocate this process's own stack, using our kernel heap
    // (a simplification for now — real processes would get PMM frames
    // mapped into their own address space; we'll refine this once
    // per-process paging is wired in).
    uint8_t *stack = (uint8_t *)kmalloc(PROCESS_STACK_SIZE);
    if (stack == NULL) {
        kfree(proc);
        return NULL;
    }

    uint64_t *stack_top = (uint64_t *)(stack + PROCESS_STACK_SIZE);
    uint64_t *real_stack_top = stack_top; // remember the genuine top BEFORE pushing the fake frame

    // Build a FAKE saved-context stack frame, matching exactly what
    // isr_common_stub expects to pop, so this process can be "resumed"
    // for the very first time using the same restore logic.
    // NOTE: iretq in 64-bit long mode ALWAYS pops all 5 values (RIP, CS,
    // RFLAGS, RSP, SS), even ring0->ring0 — so RSP here must be a real,
    // valid address, not a placeholder.
    *(--stack_top) = 0x10;                    // ss
    *(--stack_top) = (uint64_t)real_stack_top; // rsp — genuinely valid stack top
    *(--stack_top) = 0x202;                   // rflags (interrupts enabled)
    *(--stack_top) = 0x08;                    // cs (kernel code segment)
    *(--stack_top) = (uint64_t)entry_point;   // rip — where execution begins!
    // 15 general-purpose registers, all zero for a fresh process
    for (int i = 0; i < 15; i++) {
        *(--stack_top) = 0;
    }

    proc->rsp = (uint64_t)stack_top;
    proc->pml4_phys = 0; // TODO: per-process address space, coming later

    proc->next = process_list;
    process_list = proc;

    return proc;
}

void scheduler_run_next(void) {
    if (process_list == NULL) {
        return; // nothing to run
    }

    struct process *prev = current_process;

    if (current_process == NULL) {
        current_process = process_list;
    } else {
        current_process = (current_process->next != NULL) ? current_process->next : process_list;
    }

    current_process->state = PROCESS_RUNNING;

    if (prev == NULL) {
        // First-ever switch: nothing to save, just jump straight in.
        uint64_t throwaway;
        context_switch(&throwaway, current_process->rsp);
    } else {
        prev->state = PROCESS_READY;
        context_switch(&prev->rsp, current_process->rsp);
    }
}
