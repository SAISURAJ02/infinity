#include "paging.h"
#include "pmm.h"
#include <stddef.h>
#include "limine.h"

static volatile struct limine_kernel_address_request kernel_address_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0
};

static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

#define ENTRIES_PER_TABLE 512

typedef uint64_t page_table_t[ENTRIES_PER_TABLE];

static page_table_t *pml4;

static uint64_t pml4_index(uint64_t vaddr) { return (vaddr >> 39) & 0x1FF; }
static uint64_t pdpt_index(uint64_t vaddr) { return (vaddr >> 30) & 0x1FF; }
static uint64_t pd_index(uint64_t vaddr)   { return (vaddr >> 21) & 0x1FF; }
static uint64_t pt_index(uint64_t vaddr)   { return (vaddr >> 12) & 0x1FF; }

static page_table_t *get_or_create_table(page_table_t *table, uint64_t index, uint64_t flags) {
    if (!((*table)[index] & PAGE_PRESENT)) {
        void *new_frame = pmm_alloc_frame();
        uint64_t *new_table = (uint64_t *)new_frame;
        for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
            new_table[i] = 0;
        }
        (*table)[index] = (uint64_t)new_frame | PAGE_PRESENT | flags;
    }
    return (page_table_t *)((*table)[index] & ~0xFFFULL);
}

uint64_t paging_virt_to_phys_hhdm(uint64_t virt_addr) {
    return virt_addr - hhdm_request.response->offset;
}

extern char kernel_end;

void paging_init(uint64_t fb_phys_addr, uint64_t fb_size) {
    pml4 = (page_table_t *)pmm_alloc_frame();
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        (*pml4)[i] = 0;
    }

    uint64_t kernel_virt_base = kernel_address_request.response->virtual_base;
    uint64_t kernel_phys_base = kernel_address_request.response->physical_base;
    uint64_t kernel_size = (uint64_t)&kernel_end - kernel_virt_base;
    uint64_t num_pages = (kernel_size + 4095) / 4096;

    for (uint64_t i = 0; i < num_pages; i++) {
        uint64_t offset = i * 4096;
        paging_map(kernel_virt_base + offset, kernel_phys_base + offset, PAGE_WRITABLE);
    }

    #define STACK_PAGES 4
    #define STACK_VIRT_TOP 0xFFFFFFFFA0000000ULL

    for (int i = 0; i < STACK_PAGES; i++) {
        void *frame = pmm_alloc_frame();
        uint64_t virt = STACK_VIRT_TOP - (i * 4096);
        paging_map(virt, (uint64_t)frame, PAGE_WRITABLE);
    }

    uint64_t fb_pages = (fb_size + 4095) / 4096;
    for (uint64_t i = 0; i < fb_pages; i++) {
        uint64_t offset = i * 4096;
        paging_map(fb_phys_addr + offset, fb_phys_addr + offset, PAGE_WRITABLE);
    }
}

void paging_map(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    page_table_t *pdpt = get_or_create_table(pml4, pml4_index(virt_addr), flags);
    page_table_t *pd   = get_or_create_table(pdpt, pdpt_index(virt_addr), flags);
    page_table_t *pt   = get_or_create_table(pd,   pd_index(virt_addr), flags);
    (*pt)[pt_index(virt_addr)] = (phys_addr & ~0xFFFULL) | PAGE_PRESENT | flags;
}
