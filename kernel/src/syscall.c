#include "syscall.h"

struct registers {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
};

extern void keyboard_flash(void); // reuse as a simple visible "syscall worked" signal

void syscall_handler(struct registers *regs) {
    uint64_t syscall_number = regs->rax;

    switch (syscall_number) {
        case 0: // SYS_TEST — just proves the syscall pipeline works
            keyboard_flash();
            regs->rax = 42; // arbitrary return value, to prove data flows back too
            break;
        default:
            regs->rax = (uint64_t)-1; // unknown syscall
            break;
    }
}
