# Infinity OS — Day 5

**Date:** August 23, 2026
**Focus:**Paging (virtual memory) implementation

---

## 1. Recap / Starting Point

Completed (boot, GDT, IDT, PIC/hardware interrupts). Month 2's first piece, the Physical Memory Manager (bitmap-based frame allocator using Limine's memory map), was completed and tested in the previous session. Today's focus: paging / virtual memory — the next layer in Month 2.

---

## 2. Concept: Why Paging, and Its Relationship to the Planned Security Feature

Clarified an important scoping question: capability-based security (Infinity's signature feature) is still planned for **Month 3**, since it requires processes to exist. However, paging (Month 2, now) is the direct **hardware foundation** capabilities will be built on top of — page table entries carry permission bits (readable/writable/executable, privilege level) that provide the actual enforcement mechanism. Capabilities in Month 3 will be a structured way of managing and handing out this same underlying page-level access. Confirmed: build the "locks" (paging) before designing "who gets which keys" (capabilities).

---

## 3. Concept: What Paging Solves

- Currently, the kernel writes directly to raw physical addresses with zero protection — any code can touch any memory.
- Paging introduces a layer of indirection: code uses **virtual addresses**, translated to real **physical addresses** by the CPU's MMU via **page tables**, which the kernel controls.
- Three concrete benefits identified: **isolation** (two processes can use the same virtual address, mapped to different physical frames), **protection** (permission bits per mapping — read/write/execute — enforced by the CPU, triggering page faults, exception 14, on violation), and **flexibility** (a process's memory need not be physically contiguous).

---

## 4. Concept: Where Page Tables Themselves Live

Worked through the chicken-and-egg question (mirroring the earlier PMM bitmap-placement discussion): page tables need to live in physical memory, and the answer is — **allocate them via the PMM**, using `pmm_alloc_frame()` for every new table needed. Confirmed this is exactly why the PMM was built first, in the correct dependency order.

Also clarified: Limine's bootloader already sets up a temporary, minimal page table setup (required — long mode itself mandates paging be active to even boot into it). Just like the earlier decision to build Infinity's own GDT instead of relying on Limine's, the plan is to build Infinity's own page tables from scratch and fully take ownership, rather than depending on the bootloader's temporary structures long-term.

---

## 5. Concept: Virtual Address Structure (4-Level Paging)

Broke down how a 64-bit virtual address is interpreted by x86-64 paging hardware:
```
Bits 47-39: PML4 index   (9 bits, 512 entries)
Bits 38-30: PDPT index   (9 bits, 512 entries)
Bits 29-21: PD index     (9 bits, 512 entries)
Bits 20-12: PT index     (9 bits, 512 entries)
Bits 11-0:  Page offset  (12 bits — byte within the 4KB frame)
```
9 bits → 512 entries per table (2^9); 12-bit offset → matches the 4KB (2^12) frame size exactly.

Walked through the lookup process: PML4 entry → points to PDPT → points to PD → points to PT → final entry contains the actual physical frame address, then the 12-bit offset gives the exact byte.

**Key connection re-established:** if any level's entry is "not present" (nothing mapped there yet), a fresh frame must be allocated via the PMM to create that missing table — same allocate-on-demand pattern used throughout.

---

## 6. Code: `paging.h` and `paging.c`

**`paging.h`** — defines `PAGE_PRESENT` and `PAGE_WRITABLE` flag bits (packed into the otherwise-unused low 12 bits of each 64-bit entry, since physical addresses are always 4KB-aligned — same "reuse spare bits" trick conceptually similar to the GDT's access byte), plus `paging_init()` and `paging_map()` declarations.

**`paging.c`** — implemented:
- `pml4_index`, `pdpt_index`, `pd_index`, `pt_index` — extract each 9-bit index from a virtual address via shift + mask
- `get_or_create_table()` — given a table and index, returns the next-level table, allocating and zeroing a fresh frame via `pmm_alloc_frame()` if the entry doesn't exist yet
- `paging_init()` — allocates and zeroes the top-level PML4 table
- `paging_map(virt_addr, phys_addr, flags)` — walks PML4 → PDPT → PD → PT, creating any missing tables along the way, then writes the final physical address + flags into the last-level (PT) entry

**Not yet done:** the new PML4 is not yet loaded into the CPU (CR3 register) — Limine's temporary tables are still active. This is intentional; see below.

---

## 7. Concept: The CR3 Switch — Why It's the Riskiest Step So Far

Discussed at length before writing any switch-over code, given how unforgiving this step is:

- **CR3** is the CPU register holding the physical address of the currently active PML4. The instant we write our new PML4's address into CR3, *every subsequent memory access* — starting with the very next instruction fetch — uses our new tables.
- **The real danger:** it's not about "jumping" anywhere different — the CPU is already executing sequentially. The risk is that the *next instruction immediately after* the `mov cr3` itself must still correctly resolve under the new tables, or the CPU faults instantly, likely before even reaching our exception handlers (since handling a fault itself depends on IDT/GDT/stack being reachable).
- **Required mappings before switching:** the kernel's own code/data (at its actual higher-half linked address, `0xffffffff80000000`), the current stack, and the framebuffer (if drawing is still needed afterward).
- **Safety strategy discussed:** identity-map (virtual == physical) the first several MB of physical memory as a fallback safety net, *in addition to* the proper higher-half kernel mapping — not because it's technically required if everything else is mapped correctly, but as insurance against a subtle, easy-to-make mapping bug causing an unrecoverable, silent crash at the single most fragile moment in the kernel.

**Decision:** proceed carefully and incrementally — explicitly map kernel, stack, and framebuffer first, verify each piece, before ever attempting the actual CR3 switch. Not rushing this step given its failure mode is effectively undebuggable if done wrong.

---

## 8. Next Steps

- [ ] Identify and map the kernel's actual physical/virtual load addresses (from the linker script / Limine's kernel address response)
- [ ] Map the current stack
- [ ] Map the framebuffer
- [ ] Identity-map a safety-net region of low physical memory
- [ ] Only then: perform the CR3 switch, carefully, with a fallback plan if it fails
- [ ] Test thoroughly (expect this to be the most crash-prone milestone yet)
- [ ] After paging is fully working: kernel heap allocator (`malloc`/`free`)

---

*Log — Day 6, Infinity OS project.*
