#include "process.h"
#include "pmm.h"
#include "heap.h"
#include <stddef.h>
#include "pic.h"
#include "paging.h"

static uint64_t next_pid = 1;
static struct process *process_list = NULL;
static struct process *current_process = NULL;

#define PROCESS_STACK_SIZE (4 * 4096) // 16KB per process, same as our kernel stack

void process_init(void) {
    process_list = NULL;
    current_process = NULL;
    next_pid = 1;
}

#define ENTRIES_PER_TABLE 512
typedef uint64_t page_table_t[ENTRIES_PER_TABLE];

static uint64_t create_process_pml4(void) {
    page_table_t *new_pml4 = (page_table_t *)pmm_alloc_frame();
    page_table_t *kernel_pml4 = (page_table_t *)paging_get_pml4();

    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        if (i >= 256) {
            // Higher half: identical across every process, so the kernel
            // (interrupts, syscalls, scheduler) keeps working no matter
            // which process's address space is currently active.
            (*new_pml4)[i] = (*kernel_pml4)[i];
        } else {
            // Lower half: unique per process (user-space memory).
            // Empty for now — we'll populate this once processes actually
            // get their own user-space allocations.
            (*new_pml4)[i] = 0;
        }
    }

    return (uint64_t)new_pml4;
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

    // Dummy int_no/err_code, matching what irq0's real entry pushes —
    // irq0_stub's epilogue always skips 16 bytes here before iretq.
    *(--stack_top) = 32;                      // dummy int_no
    *(--stack_top) = 0;                       // dummy err_code

    // 15 general-purpose registers, all zero for a fresh process
    for (int i = 0; i < 15; i++) {
        *(--stack_top) = 0;
    }

    proc->rsp = (uint64_t)stack_top;
    proc->pml4_phys = create_process_pml4();
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
        load_cr3(current_process->pml4_phys);
        context_switch(&throwaway, current_process->rsp);
    } else {
        prev->state = PROCESS_READY;
        load_cr3(current_process->pml4_phys);
        context_switch(&prev->rsp, current_process->rsp);
    }
}
// Called from irq0_stub on every timer tick. Given the interrupted
// process's saved RSP, saves it, advances to the next process, sends
// the PIC EOI, and returns the RSP to resume.
uint64_t schedule(uint64_t current_rsp) {
    // ALWAYS acknowledge the timer interrupt first, before any early return —
    // otherwise the PIC's in-service register stays stuck, and it will
    // never deliver another interrupt (IRQ0 or otherwise) again.
    pic_send_eoi(0);

    if (current_process != NULL) {
        current_process->rsp = current_rsp;
        current_process->state = PROCESS_READY;
    }

    if (process_list == NULL) {
        return current_rsp; // no processes to schedule, just resume as-is
    }

    current_process = (current_process != NULL && current_process->next != NULL)
                       ? current_process->next
                       : process_list;

    current_process->state = PROCESS_RUNNING;

    load_cr3(current_process->pml4_phys);

    return current_process->rsp;
}
