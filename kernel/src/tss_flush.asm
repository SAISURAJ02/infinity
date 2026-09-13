[bits 64]
global tss_flush

tss_flush:
    mov ax, 0x28    ; selector = index 5 * 8 = 0x28
    ltr ax
    ret
