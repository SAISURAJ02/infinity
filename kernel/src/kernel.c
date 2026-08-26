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

// Captured BEFORE the CR3 switch, since Limine's response structures
// themselves are not guaranteed to be mapped after we switch page tables.
static uint32_t *g_fb_ptr;
static uint64_t  g_fb_width;
static uint64_t  g_fb_height;
static uint64_t  g_fb_pitch;

static void hcf(void) {
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void isr_screen_halt(uint32_t color) {
    for (uint32_t y = 0; y < g_fb_height; y++) {
        for (uint32_t x = 0; x < g_fb_width; x++) {
            g_fb_ptr[y * (g_fb_pitch / 4) + x] = color;
        }
    }
    hcf();
}

void keyboard_flash(void) {
    static uint32_t toggle = 0x0000FF00;
    for (uint32_t y = 0; y < 20; y++) {
        for (uint32_t x = 0; x < 20; x++) {
            g_fb_ptr[y * (g_fb_pitch / 4) + x] = toggle;
        }
    }
    toggle = (toggle == 0x0000FF00) ? 0x000000FF : 0x0000FF00;
}

// Runs AFTER the CR3 switch, on our own dedicated stack.
// Uses only the pre-captured g_fb_* globals — never touches
// framebuffer_request again, since it's unmapped under our new tables.
static void kernel_post_paging(void) {
    __asm__ volatile ("sti");

    for (uint64_t y = 50; y < 250; y++) {
        for (uint64_t x = 50; x < 250; x++) {
            g_fb_ptr[y * (g_fb_pitch / 4) + x] = 0x0000FF00; // green
        }
    }

    hcf();
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

    // Capture everything we'll need AFTER the switch, right now,
    // while Limine's structures are still safely accessible.
    g_fb_ptr    = (uint32_t *)fb->address;
    g_fb_width  = fb->width;
    g_fb_height = fb->height;
    g_fb_pitch  = fb->pitch;

    uint64_t fb_phys_addr = paging_virt_to_phys_hhdm((uint64_t)fb->address);
    uint64_t fb_size = fb->pitch * fb->height;
    paging_init((uint64_t)fb->address, fb_phys_addr, fb_size);

    __asm__ volatile ("cli");
    paging_switch(paging_get_pml4(), 0xFFFFFFFFA0000000ULL, kernel_post_paging);

    hcf();
}
