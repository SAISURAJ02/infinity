#include "process.h"
#include "pmm.h"
#include "heap.h"
#include <stddef.h>
#include "pic.h"
#include "paging.h"
#include "tss.h"

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
    proc->kstack_top = (uint64_t)real_stack_top;  // ring-0-only process: this IS its kernel stack
    proc->ustack_top = 0;                          // no separate user stack — never runs at ring 3
    proc->next = process_list;
    process_list = proc;

    return proc;
}

#define USER_CODE_VADDR  0x400000ULL
#define USER_STACK_VADDR 0x500000ULL
#define USER_CS (0x18 | 3)   // user code selector, RPL=3 -> 0x1B
#define USER_SS (0x20 | 3)   // user data selector, RPL=3 -> 0x23

// mov eax, 0 ; int 0x80 ; jmp $
// Calls SYS_TEST (rax=0), which runs keyboard_flash() in the kernel —
// a visible signal that ring 3 -> syscall -> ring 0 -> back to ring 3
// all actually worked, then spins forever so it doesn't run off the page.
static const uint8_t user_test_code[] = {
    0xB8, 0x00, 0x00, 0x00, 0x00,
    0xCD, 0x80,
    0xEB, 0xFE
};

struct process *process_create_user(void) {
    struct process *proc = (struct process *)kmalloc(sizeof(struct process));
    if (proc == NULL) {
        return NULL;
    }

    proc->pid = next_pid++;
    proc->state = PROCESS_READY;
    proc->capability_count = 0;
    proc->pml4_phys = create_process_pml4();

    uint8_t *kstack = (uint8_t *)kmalloc(PROCESS_STACK_SIZE);
    if (kstack == NULL) {
        kfree(proc);
        return NULL;
    }
    proc->kstack_top = (uint64_t)(kstack + PROCESS_STACK_SIZE);

    // Write the test code into a fresh physical frame via its safe HHDM
    // view, then map that SAME frame into the process's own tables at
    // USER_CODE_VADDR, marked PAGE_USER.
    uint64_t code_phys = (uint64_t)pmm_alloc_frame();
    uint8_t *code_hhdm = (uint8_t *)paging_phys_to_virt_hhdm(code_phys);
    for (uint32_t i = 0; i < sizeof(user_test_code); i++) {
        code_hhdm[i] = user_test_code[i];
    }
    paging_map_into(proc->pml4_phys, USER_CODE_VADDR, code_phys, PAGE_USER);

    // A separate page for the ring-3 stack.
    uint64_t ustack_phys = (uint64_t)pmm_alloc_frame();
    paging_map_into(proc->pml4_phys, USER_STACK_VADDR, ustack_phys, PAGE_WRITABLE | PAGE_USER);
    proc->ustack_top = USER_STACK_VADDR + 4096;

    // Fake iretq frame, built on the KERNEL stack — but this time the
    // rsp/cs/ss fields are all real and load-bearing, since this is a
    // ring0 -> ring3 transition (5-value iretq, from the rule we worked
    // through earlier).
    uint64_t *stack_top = (uint64_t *)proc->kstack_top;

    *(--stack_top) = USER_SS;
    *(--stack_top) = proc->ustack_top;
    *(--stack_top) = 0x202;
    *(--stack_top) = USER_CS;
    *(--stack_top) = USER_CODE_VADDR;

    *(--stack_top) = 32;
    *(--stack_top) = 0;

    for (int i = 0; i < 15; i++) {
        *(--stack_top) = 0;
    }

    proc->rsp = (uint64_t)stack_top;
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
        tss_set_rsp0(current_process->kstack_top);
        context_switch(&throwaway, current_process->rsp);
    } else {
        prev->state = PROCESS_READY;
        load_cr3(current_process->pml4_phys);
        tss_set_rsp0(current_process->kstack_top);
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
    tss_set_rsp0(current_process->kstack_top);

    return current_process->rsp;
}
