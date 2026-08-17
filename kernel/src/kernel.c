#include <stdint.h>
#include <stddef.h>
#include "limine.h"

// Ask Limine for a framebuffer (graphics come later; for now this just
// confirms the boot handoff worked correctly)
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

    // Draw a single white pixel at (100, 100) — proof the kernel is alive
    uint32_t *fb_ptr = (uint32_t *)fb->address;
    fb_ptr[100 * (fb->pitch / 4) + 100] = 0xFFFFFFFF;

    hcf();
}
