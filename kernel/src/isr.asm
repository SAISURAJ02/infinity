[bits 64]

extern isr_handler

; Macro for exceptions that do NOT push an error code automatically —
; we push a dummy 0 ourselves so the stack layout matches the ones that do.
%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push qword 0        ; dummy error code
    push qword %1        ; exception number
    jmp isr_common_stub
%endmacro

; Macro for exceptions that DO push an error code automatically —
; we only need to push the exception number.
%macro ISR_ERRCODE 1
global isr%1
isr%1:
    push qword %1        ; exception number
    jmp isr_common_stub
%endmacro

ISR_NOERRCODE 0    ; divide-by-zero
ISR_NOERRCODE 6    ; invalid opcode
ISR_ERRCODE   13   ; general protection fault
ISR_ERRCODE   14   ; page fault

isr_common_stub:
    ; Save all general-purpose registers so the C handler
    ; can inspect them, and so we can restore them afterward.
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

    mov rdi, rsp     ; pass a pointer to all this saved data as the argument to isr_handler
    call isr_handler

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

    add rsp, 16      ; remove the error code + exception number we pushed
    iretq            ; proper interrupt return — restores RIP, CS, RFLAGS, RSP, SS
