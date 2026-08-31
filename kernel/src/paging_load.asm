[bits 64]
global paging_switch

; rdi = new PML4 physical address
; rsi = new stack top (a virtual address already mapped in the new tables)
; rdx = continuation function to jump into (never returns here)
paging_switch:
    mov cr3, rdi
    mov rsp, rsi
    xor rbp, rbp
    jmp rdx
global load_cr3
load_cr3:
    mov cr3, rdi
    ret
