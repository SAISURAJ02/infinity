[bits 64]
extern irq_handler
extern schedule

global irq0
irq0:
    push qword 0      ; dummy error code, for consistent stack layout
    push qword 32      ; interrupt number
    jmp irq0_stub

global irq1
irq1:
    push qword 0
    push qword 33      ; interrupt number
    jmp irq_common_stub

; --- Timer-specific stub: routes through the scheduler ---
irq0_stub:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp          ; pass current RSP as the argument to schedule()
    call schedule          ; schedule() returns the NEXT process's RSP to use
    mov rsp, rax           ; switch to that process's saved stack

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16
    iretq

; --- Shared stub for all other interrupts (keyboard, exceptions, etc.) ---
irq_common_stub:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    call irq_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16
    iretq
