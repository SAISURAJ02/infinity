#include <stdint.h>
#include <stddef.h>
#include "limine.h"

static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

static void hcf(void) {
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void kernel_main(void) {
    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    uint32_t *fb_ptr = (uint32_t *)fb->address;

    for (uint64_t y = 50; y < 250; y++) {
        for (uint64_t x = 50; x < 250; x++) {
            fb_ptr[y * (fb->pitch / 4) + x] = 0x00FF0000; // red
        }
    }

    hcf();
}
