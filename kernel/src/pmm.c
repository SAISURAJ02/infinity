#include "pmm.h"
#include "limine.h"
#include<paging.h>

#define FRAME_SIZE 4096

static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

static uint8_t *bitmap;
static uint64_t total_frames;
static uint64_t highest_addr = 0;

static void bitmap_set(uint64_t frame) {
    bitmap[frame / 8] |= (1 << (frame % 8));
}

static void bitmap_clear(uint64_t frame) {
    bitmap[frame / 8] &= ~(1 << (frame % 8));
}

static int bitmap_test(uint64_t frame) {
    return bitmap[frame / 8] & (1 << (frame % 8));
}


uint64_t pmm_get_highest_addr(void) {
    return highest_addr;
}

void pmm_init(void) {
    struct limine_memmap_response *memmap = memmap_request.response;

    // Pass 1: find the highest usable address, to know how big our bitmap needs to be
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            uint64_t end = entry->base + entry->length;
            if (end > highest_addr) highest_addr = end;
        }
    }

    total_frames = highest_addr / FRAME_SIZE;
    uint64_t bitmap_size = (total_frames + 7) / 8; // bytes needed, rounded up

    // Pass 2: find a USABLE region large enough to hold the bitmap itself
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= bitmap_size) {
            bitmap = (uint8_t *)paging_phys_to_virt_hhdm(entry->base);
            break;
        }
    }

    // Start by marking EVERYTHING as used (safe default) — we'll clear the
    // actually-usable bits next.
    for (uint64_t i = 0; i < bitmap_size; i++) {
        bitmap[i] = 0xFF;
    }

    // Pass 3: mark every USABLE region's frames as free
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            uint64_t start_frame = entry->base / FRAME_SIZE;
            uint64_t frame_count = entry->length / FRAME_SIZE;
            for (uint64_t f = 0; f < frame_count; f++) {
                bitmap_clear(start_frame + f);
            }
        }
    }

    // Finally, re-mark the bitmap's own storage as used, so nothing
    // allocates over it later.
    // Finally, re-mark the bitmap's own storage as used, so nothing
    // allocates over it later.
    uint64_t bitmap_start_frame = paging_virt_to_phys_hhdm((uint64_t)bitmap) / FRAME_SIZE;
    uint64_t bitmap_frame_count = (bitmap_size + FRAME_SIZE - 1) / FRAME_SIZE;
    for (uint64_t f = 0; f < bitmap_frame_count; f++) {
        bitmap_set(bitmap_start_frame + f);
    }

    // Reserve frame 0 so pmm_alloc_frame() never returns physical address
    // 0x0, which callers correctly treat as NULL/failure.
    bitmap_set(0);
}
void *pmm_alloc_frame(void) {
    for (uint64_t i = 0; i < total_frames; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            return (void *)(i * FRAME_SIZE);
        }
    }
    return NULL; // out of memory
}

void pmm_free_frame(void *addr) {
    uint64_t frame = (uint64_t)addr / FRAME_SIZE;
    bitmap_clear(frame);
}
