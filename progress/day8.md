# Infinity OS — Day 8

**Date:** August 27, 2026
**Focus:** Kernel heap allocator (kmalloc/kfree) — design, implementation, and a genuinely important bug fix

---

## 1. Recap / Starting Point

With paging fully working (Day 8), moved on to building a heap allocator — the mechanism for handing out variable-sized memory chunks, rather than only whole 4KB frames via the PMM.

---

## 2. Design Discussion: Virtual Memory Layout

Before writing code, addressed an open item flagged back on Day 6: previously, virtual addresses (like the kernel stack's location) were picked somewhat arbitrarily. Established a more deliberate, documented layout instead:
```
0xffffffff80000000 — Kernel code/data (fixed, from the linker script)
0xffffffff90000000 — Kernel heap
0xffffffffa0000000 — Kernel stack
```
Regions spaced 256MB apart to leave room for growth without collision — a small but real improvement in engineering discipline over the earlier ad hoc approach.

---

## 3. Designing the Block Header

Discussed the classic free-list allocator approach: a small header placed immediately before each block of usable memory, containing `size` (usable bytes, not including the header itself), `free` (flag), and `next` (pointer to the next block in the list).

Worked through *why* `size` excludes the header's own footprint — the header physically consumes real bytes at the start of a region, so `size` must describe only what's actually available to a caller, or later calculations would be off by `sizeof(header)`.

---

## 4. Implementing `heap_init()`, `kmalloc()`, `kfree()`

- **`heap_init()`** — allocates 16 physical frames (64KB) via the PMM, maps them at the newly-designated heap virtual address range, and initializes the whole region as one single free block.
- **`kmalloc(size)`** — first version: walks the linked list looking for a free block big enough, marks it used, and returns a pointer just past the header (`current + 1` — pointer arithmetic that advances by exactly `sizeof(header)` bytes).
- **`kfree(ptr)`** — recovers the header's address by subtracting 1 from the given pointer (`(header *)ptr - 1`), the exact reverse of what `kmalloc` did, then flips `free` back to `1`.

Worked through the pointer arithmetic carefully — specifically clarifying that `kfree` cannot reuse any variable from the original `kmalloc` call (it's long gone by the time `kfree` runs); it must independently recompute the header's location from the one pointer it's given.

---

## 5. Testing — First Real Bug: No Block Splitting

Built an incremental visual test (reusing the established color-coded proof-of-life pattern): distinct colors for "first allocation failed," "second allocation failed," "allocations overlap," and "success."

**Result: magenta** — meaning the first (64-byte) allocation succeeded, but the second (128-byte) allocation failed.

**Root cause identified:** `heap_init()` creates exactly **one** block covering the entire 64KB region. The first `kmalloc()` call marked that *entire* block as used, regardless of how much was actually requested — leaving no remaining free block for anything else to find. This wasn't a minor bug; without splitting, the allocator could only ever satisfy a single allocation, ever, making it non-functional in practice.

**Fix — implemented block splitting in `kmalloc`:** when a free block found is significantly larger than requested, carve out a new block header from the leftover space (positioned right after the memory being handed out), give it the remaining size, mark it free, and link it into the list; shrink the original block's `size` down to exactly what was requested before marking it used.

**Result after the fix: green** — both allocations succeeded at distinct, non-overlapping addresses.

---

## 6. Full End-to-End Test

Extended the test to also write real data into both allocated blocks, free one, and confirm a subsequent same-size allocation correctly **reused** that exact freed block's address — proving `kmalloc`/`kfree` genuinely work together, not just independently avoid crashing.

**Hit two path/syntax slips along the way, both instructive:**
- Accidentally ran `gedit ../kernel.c` while already inside `kernel/src/` — since `..` moves up a directory, this opened/created a blank file in the wrong location (`kernel/kernel.c` instead of `kernel/src/kernel.c`). Caught before saving anything into it; resolved by using the plain filename when already in the correct directory.
- A missing `for (uint64_t y = 50; y < 250; y++) {` line during a manual edit caused a real compile error (`'y' undeclared`) that cascaded into confusing, unrelated-looking errors further down (a conflicting-types error on `hcf`, a stray brace). Diagnosed precisely by printing the exact line range with `sed -n` rather than guessing, which immediately made the missing line obvious.

**Final result after fixing both: green square, full test passing.** The heap allocator is genuinely working end-to-end — allocation, correct splitting, real memory writes, freeing, and correct reuse of freed space.

---

## 7. Cleanup

Removed the temporary color-coded test logic from `kernel_post_paging`, restoring it to simply call `heap_init()` and draw the standard green success square — keeping the boot path clean now that the allocator is proven.

---

## 8. Reflection

The block-splitting bug was a good one to hit and fix properly — it's the kind of gap that "looks done" (compiles, first allocation works) but is fundamentally non-functional beyond the simplest case. Catching it via a deliberately incremental, color-coded test (rather than one big pass/fail check) made the exact failure point immediately obvious, continuing the diagnostic discipline established during the paging debugging earlier this week.

---

## 9. Next Steps

- [ ] Confirm final cleanup build still shows the green square
- [ ] Commit the working heap allocator
- [ ] Consider (not urgent): block-merging on free, to reduce fragmentation over time — noted as a future refinement, not required for current functionality
- [ ] Move on to the next planned piece of work

---

*Log — Day 8, Infinity OS project.*
