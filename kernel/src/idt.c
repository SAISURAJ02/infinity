#include "idt.h"
#include "pic.h"
#include "io.h"

// One IDT entry, packed exactly as the CPU expects it (64-bit mode format)
struct idt_entry {
    uint16_t offset_low;    // handler address, bits 0-15
    uint16_t selector;      // GDT code segment selector (0x08 = our kernel code segment)
    uint8_t  ist;           // interrupt stack table (0 = not used, for now)
    uint8_t  type_attr;     // gate type + privilege level + present bit
    uint16_t offset_mid;    // handler address, bits 16-31
    uint32_t offset_high;   // handler address, bits 32-63
    uint32_t zero;          // reserved, must be 0
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));
struct registers {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

extern void isr_screen_halt(uint32_t color); // defined in kernel.c
extern void keyboard_flash(void);            // defined in kernel.c

void isr_handler(struct registers *regs) {
    // Different color per exception, so a crash is visually identifiable
    // instead of the old silent triple-fault reboot.
    uint32_t color;
    switch (regs->int_no) {
        case 0:  color = 0x00FFFF00; break; // yellow: divide-by-zero
        case 6:  color = 0x00FF00FF; break; // magenta: invalid opcode
        case 13: color = 0x0000FFFF; break; // cyan: general protection fault
        case 14: color = 0x00FFFFFF; break; // white: page fault
        default: color = 0x00888888; break;
    }
    isr_screen_halt(color);
}
// Handles all non-timer hardware interrupts (currently just IRQ1/keyboard —
// the timer/IRQ0 now routes through irq0_stub -> schedule() directly,
// since preemptive scheduling needs to happen at the assembly level).
void irq_handler(struct registers *regs) {
    if (regs->int_no == 33) {
        uint8_t scancode = inb(0x60);
        if (!(scancode & 0x80)) {
            // key press — toggle a small indicator pixel, without halting,
            // so the kernel keeps running normally after every keystroke
            keyboard_flash();
        }
    }
    pic_send_eoi(regs->int_no - 32);
}

static struct idt_entry idt[256];
static struct idt_ptr   idtp;

extern void idt_load(uint64_t);

// Assembly stubs for the first few CPU exceptions (we'll add these next)
extern void isr0(void);   // divide-by-zero
extern void isr6(void);   // invalid opcode
extern void isr13(void);  // general protection fault
extern void isr14(void);  // page fault
extern void irq0(void);
extern void irq1(void);
static void idt_set_entry(int n, uint64_t handler, uint16_t selector, uint8_t type_attr) {
    idt[n].offset_low  = handler & 0xFFFF;
    idt[n].offset_mid  = (handler >> 16) & 0xFFFF;
    idt[n].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[n].selector    = selector;
    idt[n].ist         = 0;
    idt[n].type_attr   = type_attr;
    idt[n].zero        = 0;
}

void idt_init(void) {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base  = (uint64_t)&idt;

    // 0x8E = present, ring 0, 64-bit interrupt gate
    idt_set_entry(0,  (uint64_t)isr0,  0x08, 0x8E);
    idt_set_entry(6,  (uint64_t)isr6,  0x08, 0x8E);
    idt_set_entry(13, (uint64_t)isr13, 0x08, 0x8E);
    idt_set_entry(14, (uint64_t)isr14, 0x08, 0x8E);
    idt_set_entry(32, (uint64_t)irq0, 0x08, 0x8E);
    idt_set_entry(33, (uint64_t)irq1, 0x08, 0x8E);

    idt_load((uint64_t)&idtp);
}
