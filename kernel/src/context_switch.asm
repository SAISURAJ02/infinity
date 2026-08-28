[bits 64]
global context_switch

; rdi = pointer to save the OLD process's rsp into
; rsi = the NEW process's saved rsp to switch to
context_switch:
    ; Save all general-purpose registers of the currently running process
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

    mov [rdi], rsp   ; save current stack pointer into the OLD process's rsp field

    mov rsp, rsi      ; switch to the NEW process's saved stack pointer

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

    add rsp, 16       ; skip dummy err_code and int_no (matches process_create's frame)
    iretq             ; jump into the new process's saved rip
