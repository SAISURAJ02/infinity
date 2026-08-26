# Infinity OS — Day 7

**Date:** August 25, 2026
**Focus:** Finishing the paging/CR3 switch debugging from day 6, then starting the kernel heap allocator

---

## 1. Recap / Starting Point

Picked up from Day 6 with a clear diagnosis in hand but the fix not yet implemented: the CR3 switch was triple-faulting because Limine's original boot stack lived in the HHDM virtual region, and a plain `ret`-based switch tried to read a return address from that now-unmapped stack after the switch. The plan was to replace the switch with a proper trampoline.

---

## 2. Implementing the Trampoline Fix

Cross-checked the Day 6 diagnosis against a second AI tool's independent analysis of the same debug log — both converged on the same root cause, plus surfaced two additional real bugs worth fixing in the same pass:

1. **Stack page offset off-by-one** — the stack pages were mapped starting *at* `STACK_VIRT_TOP` instead of *below* it, leaving only 3 of 4 pages actually usable once `RSP` started at the top and the stack grew downward. Fixed by mapping `STACK_VIRT_TOP - ((i + 1) * 4096)` instead of `STACK_VIRT_TOP - (i * 4096)`.
2. **Intermediate page table permissions** — cleaned up so intermediate tables (PDPT/PD) are always created with `PAGE_WRITABLE`, letting the leaf-level `PT` entry be the sole source of truth for actual permissions.

**Implemented the trampoline** (`paging_switch`) in `paging_load.asm`, replacing the old `paging_load_cr3`:
```nasm
paging_switch:
    mov cr3, rdi
    mov rsp, rsi
    xor rbp, rbp
    jmp rdx
```
This atomically switches CR3, switches to our own already-mapped stack, and **jumps** (never returns) into a continuation function — eliminating any reliance on the old, now-unmapped stack.

Updated `paging.h`/`paging.c` accordingly: `paging_init()` now only builds tables (the switch itself is a separate, explicit step); added `paging_get_pml4()` so the caller can retrieve the PML4's physical address; framebuffer is now mapped at its real HHDM virtual address (not identity-mapped) so `fb->address` continues to resolve correctly post-switch.

Restructured `kernel.c` to add a `kernel_post_paging()` continuation function (containing the post-switch boot logic), and call `paging_switch(paging_get_pml4(), stack_top, kernel_post_paging)` explicitly from `kernel_main`. Added `cli` right before the switch and `sti` as the first line of the continuation, closing the narrow window where a hardware interrupt could fire mid-switch and use the stale stack.

---

## 3. Build Errors — Missing PMM Accessor

First build attempt failed at the **link** stage:
```
undefined reference to `pmm_get_highest_addr'
```
Traced this to a function that had been planned on Day 6 but never actually implemented — `pmm.c` had the internal `highest_addr` variable, but no public accessor, and `pmm.h` had no declaration either. Added both:
```c
// pmm.h
uint64_t pmm_get_highest_addr(void);

// pmm.c
uint64_t pmm_get_highest_addr(void) {
    return highest_addr;
}
```
Rebuilt successfully after this.

---

## 4. First Real Test — Still Triple-Faulting (New Bug)

With the trampoline in place and linking cleanly, the first actual run **still triple-faulted** — a different failure from Day 6's.

**Debugged via QEMU's debug log again** (`-d int,cpu_reset -D qemu_debug.log --no-reboot --no-shutdown`), run in parallel with a second terminal tailing the log file.

**Pattern observed:** the exact same page fault (`CR2 = ffff80001ff69010`) repeated four times in a row at the identical instruction, with the stack shrinking each time — the signature of a handler faulting on itself repeatedly until the stack was exhausted, cascading into a double fault and then a triple fault.

**Root cause:** `kernel_post_paging()` (and `isr_screen_halt`/`keyboard_flash`) were still **re-reading `framebuffer_request.response`** after the CR3 switch — but Limine's response *structures* themselves (as opposed to the actual framebuffer pixel memory) were never explicitly mapped in the new page tables. Dereferencing them post-switch faulted immediately; the fault handler itself then tried to draw to the screen using the same unmapped structure, faulting again identically — hence the repeating pattern.

**Fix:** captured all needed framebuffer info (pointer, width, height, pitch) into plain global variables **before** the switch, while Limine's structures were still safely accessible under the original tables. Rewrote `isr_screen_halt`, `keyboard_flash`, and `kernel_post_paging` to use only these pre-captured globals, never touching `framebuffer_request` again after the switch.

---

## 5. Success ✅

Rebuilt and ran — **a green square rendered on screen**, deliberately chosen as a distinct color from the earlier red square, to unambiguously confirm execution had reached and completed `kernel_post_paging()` — meaning the CR3 switch succeeded, the new dedicated stack is valid, interrupts were safely re-enabled, and the kernel is now running entirely under its own custom 4-level page tables.

Cleaned up `qemu_debug.log` (added to `.gitignore` and deleted — a disposable diagnostic artifact from the manual debug command, not part of the actual project).

Committed:
```
git commit -m "Successfully switch to Infinity's own page tables (CR3 trampoline). Fixed HHDM stack fault and post-switch unmapped-struct access. Full paging system now active."
```

---

## 6. Reflection

This closes out a genuinely difficult two-day debugging arc spanning two distinct, non-obvious root causes: first, an unsafe `ret`-based switch reading from an unmapped HHDM stack; second, code that appeared correct but silently depended on bootloader-owned structures remaining accessible after fully replacing the bootloader's page tables. Both were only found by reading actual CPU-level fault data rather than guessing from symptoms — a pattern worth carrying forward into future debugging (the PIC masking bug earlier in the project followed the same "read real diagnostics, don't guess" lesson).

---

## 7. Starting the Kernel Heap Allocator

With paging fully working, moved to the next planned piece: a kernel heap allocator (essentially building `malloc`/`free` from scratch).

**Concept discussed:**
- Everything allocated so far has come in fixed 4KB chunks via the PMM — fine for page tables, wasteful for small, irregularly-sized allocations a kernel actually needs day to day.
- Planned approach: a **linked list of free blocks**, each with a small header (size, free/in-use flag, pointer to next block) placed just before the usable memory it describes. `malloc(size)` walks the list for a suitable free block; `free(ptr)` marks a block free again (block-merging to reduce fragmentation flagged as a later refinement, not needed for a first working version).

**Open question, not yet resolved:** where the heap's starting virtual address should come from — an arbitrary placeholder (as was done for the stack), or a more deliberate choice now that real paging exists, tying into the still-unaddressed "design Infinity's actual virtual memory layout properly" item from Day 6. To be picked up next session.

---

## 8. Next Steps

- [ ] Decide on the heap's virtual address range as part of a more deliberate virtual memory layout (not just another placeholder address)
- [ ] Implement the free-list block header structure
- [ ] Implement `kmalloc()` / `kfree()`
- [ ] Test with a few allocations of varying sizes, confirm no overlap/corruption
- [ ] Once heap allocator is solid: revisit and formalize Infinity's virtual memory layout properly (kernel, stack, heap, framebuffer regions clearly defined, not ad hoc)

---

*Log — Day 7, Infinity OS project.*
