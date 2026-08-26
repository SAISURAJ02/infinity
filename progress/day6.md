# Infinity OS — Day 6

**Date:** August 25, 2026
**Focus:** Month 2 — Paging: kernel/stack/framebuffer mapping, the CR3 switch, and a genuinely hard debugging session

---

## 1. Recap / Starting Point

Continuing from Day 5, where `paging.c` had `paging_init()` building page tables (PML4 walk logic, `get_or_create_table()`) but nothing had been mapped yet and CR3 had not been touched. Today's goal: actually map the kernel, stack, and framebuffer, then perform the CR3 switch to activate Infinity's own page tables.

---

## 2. Mapping the Kernel

- Added `limine_kernel_address_request` to get the kernel's actual virtual and physical load addresses from Limine.
- Added `kernel_end = .;` to `linker.ld`, right after the `.bss` section, to get a linker-provided symbol marking the kernel's true end address.
- Computed `kernel_size = kernel_end - virtual_base`, then `num_pages = kernel_size / 4096` (rounded up), and looped calling `paging_map()` once per page to map the kernel at its real higher-half address.

---

## 3. Mapping a Dedicated Kernel Stack

- Discussed why relying on Limine's original stack is fragile (we don't know its exact bounds) versus allocating our own via the PMM.
- Allocated 4 pages (16KB) via `pmm_alloc_frame()`, mapped downward from an arbitrary placeholder virtual address (`0xFFFFFFFFA0000000`), matching how stacks conventionally grow.
- Flagged honestly: this address is a placeholder, not a deliberate part of a real virtual memory layout yet — to be revisited later.

---

## 4. Mapping the Framebuffer (First Attempt — Identity Mapped)

- Initially mapped the framebuffer using identity mapping (`physical == physical`) as the simplest option, needing the physical address converted from Limine's HHDM-based virtual address.

---

## 5. Bug: Duplicate Limine Request (HHDM declared twice)

**Symptom:** `PANIC: limine: Conflict detected for request ID ...` — Limine refused to boot at all.

**Cause:** `LIMINE_HHDM_REQUEST` was declared in both `kernel.c` and `paging.c` — Limine requires each request type to be declared exactly once across the whole kernel binary.

**Fix:** removed the duplicate from `kernel.c`; added a `paging_virt_to_phys_hhdm()` helper function in `paging.c` so `kernel.c` could convert addresses without needing its own HHDM request.

**Verification:** rebuilt — red square rendered successfully, confirming all table-building logic (kernel mapping, stack mapping, framebuffer mapping) ran without crashing, still using Limine's original tables (CR3 not yet touched). **Committed as a safe checkpoint** before attempting the switch.

---

## 6. First CR3 Switch Attempt — Triple Fault

- Added `paging_load.asm` (`paging_load_cr3`, doing `mov cr3, rdi; ret`) and called it at the end of `paging_init()`.
- **Result: immediate triple fault**, QEMU resetting in a loop between SeaBIOS and the Limine menu.

**First hypothesis (correct in principle, incomplete in practice):** Limine's original boot stack wasn't mapped in the new tables, so the `ret` inside `paging_load_cr3` would fault trying to read a return address from unmapped memory.

**Attempted fix 1:** identity-mapped the first 64MB of physical memory as a safety net. **Still triple-faulted.**

**Attempted fix 2:** identity-mapped *all* detected physical RAM (using a new `pmm_get_highest_addr()` accessor from `pmm.c`), removing the 64MB guess entirely. **Still triple-faulted** — confirming the issue wasn't about *how much* was identity-mapped, but *what kind* of address was actually failing.

---

## 7. Real Diagnosis — QEMU Debug Log

Ran QEMU manually with full CPU-event logging instead of guessing further:
```bash
qemu-system-x86_64 -cdrom infinity.iso -m 512M -d int,cpu_reset -D qemu_debug.log --no-reboot --no-shutdown
```

**Log revealed the exact failure:**
```
0: v=0e ... IP=0008:ffffffff80000f93 ... CR2=ffff80001ff65f28
1: v=08 ... (same IP/SP)
Triple fault
```
`v=0e` = page fault, `v=08` = double fault (cascading from the first), both at the same instruction — and critically, `CR2` (faulting address) exactly matched the current `RSP` value.

**Cross-checked with a second AI tool's analysis of the same log — confirmed and sharpened the diagnosis:**
- Limine's 64-bit boot stack pointer is **not** in low physical memory at all — it's an **HHDM-region virtual address** (`0xffff8000...`), which our identity-mapping attempts (low memory only) never covered, no matter how much low memory we mapped.
- The actual fault: `mov cr3, rdi` switches tables successfully, but the **very next instruction, `ret`**, tries to pop the return address from `%rsp`, which is still Limine's original HHDM stack address — unmapped in our new tables. This triggers a page fault; since even *delivering* that page fault requires pushing an exception frame onto the same broken stack, it cascades into a double fault, then triple fault.
- **Second, related bug identified in the same pass** (not yet triggered, since we never got past the stack fault): the framebuffer was mapped via identity mapping (`physical == physical`), but `kernel.c` actually draws using `fb->address`, which is an HHDM **virtual** address — mismatched from what was mapped. This would have faulted immediately after the stack issue was fixed, if left uncorrected.

**Key lesson:** switching CR3 safely requires *never touching the old stack again* — not just "make sure enough is identity-mapped." A `ret`-based switch is fundamentally unsafe here, since `ret` inherently depends on the *current* stack still being valid post-switch.

---

## 8. The Real Fix — In Progress

Redesigning the switch-over as a proper trampoline instead of a plain function call:

- **`paging_switch(pml4_phys, new_stack_top, continuation_fn)`** (replacing `paging_load_cr3`) — a new assembly routine that atomically: loads CR3, switches `RSP` to our own dedicated (already-mapped) stack, zeroes `RBP`, then **jumps** (not calls/returns) directly into a continuation function — ensuring the old stack is never dereferenced again after the switch.
- **Framebuffer mapping corrected:** now mapping `fb_virt_addr → fb_phys_addr` (the real HHDM virtual address Limine gave us, pointing to the correct physical frame) instead of a plain identity mapping — so `fb->address` continues to resolve correctly after the switch.
- **`paging_init()` restructured** to only build tables — the actual switch is now a separate, explicit step the caller performs.
- **`kernel.c` restructuring started** — will need a continuation function containing the post-switch boot logic (red square drawing), since execution can no longer simply "fall through" past the switch the way it could when CR3 wasn't being touched.

**Not yet complete/tested as of this log** — code changes are in progress; next session picks up finishing `kernel.c`'s restructuring and testing the trampoline-based switch.

---

## 9. Reflection

This was a genuinely difficult, multi-hour debugging session — and a good one to have gone through carefully rather than by trial-and-error alone. Two rounds of "add more identity mapping" both failed for the same underlying reason (wrong *kind* of address, not wrong *amount*), which is exactly why pulling real QEMU debug logs (rather than guessing from symptoms) was the turning point. This mirrors the PIC masking bug from Day 5 in spirit: a subtle, non-obvious gap between "looks correct" and "is correct" that only real diagnostic data could reveal.

---

## 10. Next Steps

- [ ] Finish restructuring `kernel.c`: split boot logic into pre-switch setup and a post-switch continuation function
- [ ] Update the `paging_init()` call site with the corrected signature (now takes `fb_virt_addr` too)
- [ ] Call `paging_switch()` explicitly with the new stack top and continuation function
- [ ] Test — expect this to be the real resolution, but verify via the red square rendering successfully post-switch, not just "no immediate triple fault"
- [ ] Once confirmed working: commit this milestone (genuinely a significant one — Infinity fully running on its own page tables)
- [ ] Then: kernel heap allocator (`malloc`/`free`)

---

*Log — Day 6, Infinity OS project.*
