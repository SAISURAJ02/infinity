#include "syscall.h"
#include "process.h"
#include <stddef.h>

struct registers {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
};

extern void keyboard_flash(void); // reuse as a simple visible "syscall worked" signal
extern uint32_t *g_fb_ptr;
extern uint64_t  g_fb_pitch;

// SYS_WRITE_PIXEL arguments, packed into a single 64-bit value passed via rbx:
// bits 0-15:  x
// bits 16-31: y
// bits 32-63: color (only need 24 bits really, but keep it simple)
void syscall_handler(struct registers *regs) {
    uint64_t syscall_number = regs->rax;

    switch (syscall_number) {
        case 0: // SYS_TEST — just proves the syscall pipeline works
            keyboard_flash();
            regs->rax = 42;
            break;

        case 1: { // SYS_WRITE_PIXEL
            uint32_t x = (uint32_t)(regs->rbx & 0xFFFF);
            uint32_t y = (uint32_t)((regs->rbx >> 16) & 0xFFFF);
            uint32_t color = (uint32_t)regs->rcx;

            struct process *caller = process_get_current();

            if (caller != NULL && process_check_capability(caller, CAP_DRAW_REGION, x, y)) {
                g_fb_ptr[y * (g_fb_pitch / 4) + x] = color;
                regs->rax = 0; // success
            } else {
                regs->rax = (uint64_t)-1; // ACCESS DENIED — no matching capability
            }
            break;
        }

        default:
            regs->rax = (uint64_t)-1;
            break;
    }
}
