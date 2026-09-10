#include "process.h"
#include "pmm.h"
#include "heap.h"
#include <stddef.h>
#include "pic.h"
#include "paging.h"

static uint64_t next_pid = 1;
static struct process *process_list = NULL;
static struct process *current_process = NULL;

#define PROCESS_STACK_SIZE (4 * 4096)

void process_init(void) {
    process_list = NULL;
    current_process = NULL;
    next_pid = 1;
}

#define ENTRIES_PER_TABLE 512
typedef uint64_t page_table_t[ENTRIES_PER_TABLE];

static uint64_t create_process_pml4(void) {
    uint64_t new_pml4_phys = (uint64_t)pmm_alloc_frame();
    page_table_t *new_pml4 = (page_table_t *)paging_phys_to_virt_hhdm(new_pml4_phys);
    page_table_t *kernel_pml4 = (page_table_t *)paging_phys_to_virt_hhdm(paging_get_pml4());

    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        if (i >= 256) {
            // Higher half: identical across every process (kernel code,
            // stack, heap, framebuffer, AND the HHDM physical-memory
            // window) — this is what keeps interrupts/syscalls/scheduler
            // working under any process's CR3, without exposing the
            // low-half identity map to other processes.
            (*new_pml4)[i] = (*kernel_pml4)[i];
        } else {
            // Lower half: unique per process (user-space memory) —
            // genuinely isolated, zeroed out.
            (*new_pml4)[i] = 0;
        }
    }

    // IMPORTANT: return the PHYSICAL address — CR3 always takes a
    // physical address, never a virtual/HHDM one.
    return new_pml4_phys;
}

struct process *process_create(void (*entry_point)(void)) {
    struct process *proc = (struct process *)kmalloc(sizeof(struct process));
    if (proc == NULL) {
        return NULL;
    }

    proc->pid = next_pid++;
    proc->state = PROCESS_READY;
    proc->capability_count = 0;

    uint8_t *stack = (uint8_t *)kmalloc(PROCESS_STACK_SIZE);
    if (stack == NULL) {
        kfree(proc);
        return NULL;
    }

    uint64_t *stack_top = (uint64_t *)(stack + PROCESS_STACK_SIZE);
    uint64_t *real_stack_top = stack_top;

    *(--stack_top) = 0x10;
    *(--stack_top) = (uint64_t)real_stack_top;
    *(--stack_top) = 0x202;
    *(--stack_top) = 0x08;
    *(--stack_top) = (uint64_t)entry_point;

    *(--stack_top) = 32;
    *(--stack_top) = 0;

    for (int i = 0; i < 15; i++) {
        *(--stack_top) = 0;
    }

    proc->rsp = (uint64_t)stack_top;
    proc->pml4_phys = create_process_pml4();
    proc->next = process_list;
    process_list = proc;

    return proc;
}

int process_grant_capability(struct process *proc, capability_type_t type,
                              uint32_t x_min, uint32_t x_max,
                              uint32_t y_min, uint32_t y_max) {
    if (proc->capability_count >= MAX_CAPABILITIES) {
        return -1;
    }

    struct capability *cap = &proc->capabilities[proc->capability_count];
    cap->type = type;
    cap->x_min = x_min;
    cap->x_max = x_max;
    cap->y_min = y_min;
    cap->y_max = y_max;

    proc->capability_count++;
    return 0;
}

int process_check_capability(struct process *proc, capability_type_t type, uint32_t x, uint32_t y) {
    for (int i = 0; i < proc->capability_count; i++) {
        struct capability *cap = &proc->capabilities[i];

        if (cap->type == type &&
            x >= cap->x_min && x <= cap->x_max &&
            y >= cap->y_min && y <= cap->y_max) {
            return 1;
        }
    }
    return 0;
}

struct process *process_get_current(void) {
    return current_process;
}

struct process *process_get_list(void) {
    return process_list;
}

void scheduler_run_next(void) {
    if (process_list == NULL) {
        return;
    }

    struct process *prev = current_process;

    if (current_process == NULL) {
        current_process = process_list;
    } else {
        current_process = (current_process->next != NULL) ? current_process->next : process_list;
    }

    current_process->state = PROCESS_RUNNING;

    if (prev == NULL) {
        uint64_t throwaway;
        load_cr3(current_process->pml4_phys);
        context_switch(&throwaway, current_process->rsp);
    } else {
        prev->state = PROCESS_READY;
        load_cr3(current_process->pml4_phys);
        context_switch(&prev->rsp, current_process->rsp);
    }
}

uint64_t schedule(uint64_t current_rsp) {
    pic_send_eoi(0);

    if (current_process != NULL) {
        current_process->rsp = current_rsp;
        current_process->state = PROCESS_READY;
    }

    if (process_list == NULL) {
        return current_rsp;
    }

    current_process = (current_process != NULL && current_process->next != NULL)
                       ? current_process->next
                       : process_list;

    current_process->state = PROCESS_RUNNING;

    load_cr3(current_process->pml4_phys);

    return current_process->rsp;
}
