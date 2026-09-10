#include <stdint.h>
#include <stddef.h>
#include "limine.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "io.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "text.h"
#include "process.h"
#include "disk.h"
#include "ata.h"
#include "fs.h"
#include "shell.h"

static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

uint32_t *g_fb_ptr;
uint64_t  g_fb_width;
uint64_t  g_fb_height;
uint64_t  g_fb_pitch;

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

// Kept for potential future use, but no longer wired into the keyboard IRQ —
// real character input via keyboard_handle_scancode() replaced it.
void keyboard_flash(void) {
    static uint32_t toggle = 0x0000FF00;
    for (uint32_t y = 0; y < 20; y++) {
        for (uint32_t x = 0; x < 20; x++) {
            g_fb_ptr[y * (g_fb_pitch / 4) + x] = toggle;
        }
    }
    toggle = (toggle == 0x0000FF00) ? 0x000000FF : 0x0000FF00;
}

static inline uint64_t do_syscall_write_pixel(uint32_t x, uint32_t y, uint32_t color) {
    uint64_t result;
    uint64_t packed_xy = ((uint64_t)y << 16) | (uint64_t)x;
    __asm__ volatile (
        "mov $1, %%rax\n"
        "mov %1, %%rbx\n"
        "mov %2, %%rcx\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r" (result)
        : "r" (packed_xy), "r" ((uint64_t)color)
        : "rax", "rbx", "rcx"
    );
    return result;
}

// Capability demo process: HAS a capability for its region.
void test_process_1(void) {
    for (;;) {
        for (uint32_t y = 300; y < 320; y++) {
            for (uint32_t x = 50; x < 70; x++) {
                do_syscall_write_pixel(x, y, 0x000000FF);
            }
        }
    }
}

// Capability demo process: has NO capability — every write should be denied.
void test_process_2(void) {
    for (;;) {
        for (uint32_t y = 300; y < 320; y++) {
            for (uint32_t x = 100; x < 120; x++) {
                do_syscall_write_pixel(x, y, 0x00FF00FF);
            }
        }
    }
}

static void kernel_post_paging(void) {
    __asm__ volatile ("sti");

    heap_init();
    ata_init();
    fs_init();

    process_init();
    process_create(shell_run);

    scheduler_run_next();

    hcf();
}

void kernel_main(void) {
    gdt_init();
    idt_init();
    pic_remap();
    pit_init(100);
    keyboard_init();
    outb(0x21, inb(0x21) & ~0b00000011);
    __asm__ volatile ("sti");
    pmm_init();

    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }
    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];

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
