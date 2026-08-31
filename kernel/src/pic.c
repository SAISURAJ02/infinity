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
void pit_init(uint32_t frequency) {
    uint32_t divisor = 1193182 / frequency;

    outb(0x43, 0x36); // channel 0, low+high byte, mode 3 (square wave, repeating)
    outb(0x40, (uint8_t)(divisor & 0xFF));        // low byte
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF)); // high byte
}
void keyboard_init(void) {
    // Drain any leftover data in port 0x60
    while (inb(0x64) & 1) {
        inb(0x60);
    }

    // Enable the keyboard port (Command 0xAE)
    outb(0x64, 0xAE);

    // Read the Controller Configuration Byte (Command 0x20)
    outb(0x64, 0x20);
    while (!(inb(0x64) & 1)) {} // Wait until data is ready
    uint8_t config = inb(0x60);

    // Set Bit 0 (Enable IRQ1 interrupt) and clear Bit 4 (Enable clock)
    config |= (1 << 0);
    config &= ~(1 << 4);

    // Write the Configuration Byte back (Command 0x60)
    outb(0x64, 0x60);
    while (inb(0x64) & 2) {} // Wait until input buffer is clear
    outb(0x60, config);

    // Tell the keyboard device itself to start scanning (Command 0xF4)
    outb(0x60, 0xF4);
    while (inb(0x64) & 1) {
        inb(0x60); // Consume ACK
    }
}
