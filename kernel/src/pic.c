#include "pic.h"
#include "io.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

#define ICW1_INIT    0x10
#define ICW1_ICW4    0x01
#define ICW4_8086    0x01
#define PIC_EOI      0x20

void pic_remap(void) {
    // Save the current interrupt masks (which IRQs are enabled/disabled)
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    // Start initialization sequence on both PICs
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    // Tell each PIC where its interrupts should start in the IDT
    outb(PIC1_DATA, 32);   // master PIC: IRQ0-7  -> IDT entries 32-39
    io_wait();
    outb(PIC2_DATA, 40);   // slave PIC:  IRQ8-15 -> IDT entries 40-47
    io_wait();

    // Tell the PICs how they're wired together (master/slave relationship)
    outb(PIC1_DATA, 4);    // tell master PIC there's a slave at IRQ2
    io_wait();
    outb(PIC2_DATA, 2);    // tell slave PIC its own cascade identity
    io_wait();

    // Set both PICs to 8086 mode
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    // Restore the saved masks
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8)
        outb(PIC2_COMMAND, PIC_EOI);
    outb(PIC1_COMMAND, PIC_EOI);
}
