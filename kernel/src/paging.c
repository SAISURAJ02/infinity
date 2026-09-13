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

// Given a table and an index, return the next-level table —
// allocating and zeroing a fresh one via the PMM if it doesn't exist yet.
// NOTE: new tables are accessed via their HHDM (higher-half) virtual
// address, not their raw physical address — the low-half identity map
// is NOT shared with other processes, so dereferencing a raw physical
// address as a pointer would fault under any process other than the
// kernel's own original address space.
static page_table_t *get_or_create_table(page_table_t *table, uint64_t index) {
    if (!((*table)[index] & PAGE_PRESENT)) {
        void *new_frame = pmm_alloc_frame();
        uint64_t *new_table = (uint64_t *)paging_phys_to_virt_hhdm((uint64_t)new_frame);
        for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
            new_table[i] = 0;
        }
        (*table)[index] = (uint64_t)new_frame | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    }
    // Table pointers used for further traversal must ALSO go through HHDM,
    // for the same reason as above.
    uint64_t phys = (*table)[index] & ~0xFFFULL;
    return (page_table_t *)paging_phys_to_virt_hhdm(phys);
}

uint64_t paging_virt_to_phys_hhdm(uint64_t virt_addr) {
    return virt_addr - hhdm_request.response->offset;
}

uint64_t paging_phys_to_virt_hhdm(uint64_t phys_addr) {
    return phys_addr + hhdm_request.response->offset;
}

uint64_t paging_get_pml4(void) {
    return (uint64_t)pml4;
}

extern char kernel_end;

void paging_init(uint64_t fb_virt_addr, uint64_t fb_phys_addr, uint64_t fb_size) {
    pml4 = (page_table_t *)pmm_alloc_frame();
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        (*pml4)[i] = 0;
    }

    // Safety net: identity-map all detected physical memory (low half —
    // this is ONLY safe for the kernel's own original address space,
    // since it is NOT copied into other processes' PML4s).
    uint64_t max_addr = pmm_get_highest_addr();
    for (uint64_t addr = 0; addr < max_addr; addr += 4096) {
        paging_map(addr, addr, PAGE_WRITABLE);
    }

    // ALSO map physical RAM at its HHDM address, in the HIGHER half —
    // this IS shared by every process (entries 256-511 are always
    // copied), giving kernel code a safe, universal way to access
    // physical memory by pointer under ANY process's page tables,
    // without exposing the low-half identity map to other processes.
    for (uint64_t addr = 0; addr < max_addr; addr += 4096) {
        uint64_t hhdm_addr = paging_phys_to_virt_hhdm(addr);
        paging_map(hhdm_addr, addr, PAGE_WRITABLE);
    }

    // Map the kernel's own code/data at its actual higher-half address
    uint64_t kernel_virt_base = kernel_address_request.response->virtual_base;
    uint64_t kernel_phys_base = kernel_address_request.response->physical_base;
    uint64_t kernel_size = (uint64_t)&kernel_end - kernel_virt_base;
    uint64_t num_pages = (kernel_size + 4095) / 4096;

    for (uint64_t i = 0; i < num_pages; i++) {
        uint64_t offset = i * 4096;
        paging_map(kernel_virt_base + offset, kernel_phys_base + offset, PAGE_WRITABLE);
    }

    // Allocate and map a dedicated kernel stack (16KB = 4 pages).
    #define STACK_PAGES 4
    #define STACK_VIRT_TOP 0xFFFFFFFFA0000000ULL

    for (int i = 0; i < STACK_PAGES; i++) {
        void *frame = pmm_alloc_frame();
        uint64_t virt = STACK_VIRT_TOP - ((i + 1) * 4096);
        paging_map(virt, (uint64_t)frame, PAGE_WRITABLE);
    }

    // Map the framebuffer at its ACTUAL virtual address.
    uint64_t fb_pages = (fb_size + 4095) / 4096;
    for (uint64_t i = 0; i < fb_pages; i++) {
        uint64_t offset = i * 4096;
        paging_map(fb_virt_addr + offset, fb_phys_addr + offset, PAGE_WRITABLE);
    }
}

void paging_map(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    page_table_t *pdpt = get_or_create_table(pml4, pml4_index(virt_addr));
    page_table_t *pd   = get_or_create_table(pdpt, pdpt_index(virt_addr));
    page_table_t *pt   = get_or_create_table(pd,   pd_index(virt_addr));
    (*pt)[pt_index(virt_addr)] = (phys_addr & ~0xFFFULL) | PAGE_PRESENT | flags;
}

// Same walk as paging_map(), but starts from an explicitly given PML4
// instead of the kernel's own global one — needed because a freshly
// created process's page tables aren't active in CR3 yet when we build
// them (see process_create_user()).
void paging_map_into(uint64_t target_pml4_phys, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    page_table_t *target_pml4 = (page_table_t *)paging_phys_to_virt_hhdm(target_pml4_phys);
    page_table_t *pdpt = get_or_create_table(target_pml4, pml4_index(virt_addr));
    page_table_t *pd   = get_or_create_table(pdpt, pdpt_index(virt_addr));
    page_table_t *pt   = get_or_create_table(pd,   pd_index(virt_addr));
    (*pt)[pt_index(virt_addr)] = (phys_addr & ~0xFFFULL) | PAGE_PRESENT | flags;
}
