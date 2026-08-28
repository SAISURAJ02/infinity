#include "heap.h"
#include "pmm.h"
#include "paging.h"

#define HEAP_START 0xffffffff90000000ULL
#define HEAP_INITIAL_PAGES 16  // 16 * 4KB = 64KB to start

struct block_header {
    size_t size;                  // size of the USABLE memory after this header
    int free;                     // 1 = free, 0 = in use
    struct block_header *next;    // next block in the list
};

static struct block_header *heap_start;

void heap_init(void) {
    // Allocate and map the initial heap region
    for (int i = 0; i < HEAP_INITIAL_PAGES; i++) {
        void *frame = pmm_alloc_frame();
        paging_map(HEAP_START + (i * 4096), (uint64_t)frame, PAGE_WRITABLE);
    }

    // The whole region starts as ONE big free block
    heap_start = (struct block_header *)HEAP_START;
    heap_start->size = (HEAP_INITIAL_PAGES * 4096) - sizeof(struct block_header);
    heap_start->free = 1;
    heap_start->next = NULL;
}
void *kmalloc(size_t size) {
    struct block_header *current = heap_start;

    while (current != NULL) {
        if (current->free == 1 && current->size >= size) {
            // If this block is significantly bigger than needed, split it:
            // carve out a new free block from the leftover space.
            size_t remaining = current->size - size;
            if (remaining > sizeof(struct block_header)) {
                struct block_header *new_block =
                    (struct block_header *)((uint8_t *)(current + 1) + size);
                new_block->size = remaining - sizeof(struct block_header);
                new_block->free = 1;
                new_block->next = current->next;

                current->size = size;
                current->next = new_block;
            }

            current->free = 0;
            return (void *)(current + 1);
        }
        current = current->next;
    }

    return NULL;
}
void kfree(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    struct block_header *header = (struct block_header *)ptr - 1;
    header->free = 1;
}	
