#include <stdint.h>
#include <stddef.h>
#include "limine.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "io.h"
#include "pmm.h"
#include "paging.h"

static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

static void hcf(void) {
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void isr_screen_halt(uint32_t color) {
    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    uint32_t *fb_ptr = (uint32_t *)fb->address;
    for (uint32_t y = 0; y < fb->height; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            fb_ptr[y * (fb->pitch / 4) + x] = color;
        }
    }
    hcf();
}

void keyboard_flash(void) {
    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    uint32_t *fb_ptr = (uint32_t *)fb->address;
    static uint32_t toggle = 0x0000FF00;
    for (uint32_t y = 0; y < 20; y++) {
        for (uint32_t x = 0; x < 20; x++) {
            fb_ptr[y * (fb->pitch / 4) + x] = toggle;
        }
    }
    toggle = (toggle == 0x0000FF00) ? 0x000000FF : 0x0000FF00;
}

void kernel_main(void) {
    gdt_init();
    idt_init();
    pic_remap();
    outb(0x21, inb(0x21) & ~0b00000011);
    __asm__ volatile ("sti");
    pmm_init();

    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }
    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    uint32_t *fb_ptr = (uint32_t *)fb->address;

    uint64_t fb_phys_addr = paging_virt_to_phys_hhdm((uint64_t)fb->address);
    uint64_t fb_size = fb->pitch * fb->height;
    paging_init(fb_phys_addr, fb_size);

    for (uint64_t y = 50; y < 250; y++) {
        for (uint64_t x = 50; x < 250; x++) {
            fb_ptr[y * (fb->pitch / 4) + x] = 0x00FF0000;
        }
    }
    hcf();
}
