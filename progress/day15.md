# Infinity OS — Day 15

**Date:** September 10, 2026
**Focus:** Interactive shell, full ASCII font support, real keyboard input, and a real process-isolation regression found and fixed

---

With the filesystem verified working end-to-end (create, write, read, byte-for-byte match), today's focus shifted to building the interactive shell — the primary interface tying together processes, the filesystem, and the capability system.

---

## 1. Real Keyboard Character Input

Replaced the old "flash on keypress" keyboard indicator with genuine character input:
- Added a Scan Code Set 1 → ASCII lookup table (`keyboard.c`/`keyboard.h`)
- Added a small circular buffer so keystrokes captured by the interrupt handler are decoupled from when the shell actually processes them — no lost input even if the shell is momentarily busy
- Rewired `irq_handler` (keyboard IRQ) to feed this buffer instead of calling the old flash indicator

---

## 2. Discovering and Fixing a Font Gap

Built the first version of the shell and found the prompt and typed text simply weren't appearing. Diagnosed methodically:
- Confirmed the shell process itself was running correctly (proven with a raw pixel test box)
- Confirmed `draw_string` itself worked, but only for characters that happened to exist in the font table
- Root cause: the font table (`font.c`) had only ever been built for hex output (`0-9`, `A-F`) back when it was first created for printing memory addresses — it was never expanded before the shell needed full alphabet and punctuation support

**Fix:** expanded `font.c` to cover the full uppercase and lowercase alphabet, digits, space, and basic punctuation (period, comma, colon), with a corresponding update to `font_char_index()`'s mapping logic. Also caught and fixed a follow-up gap — the `>` prompt character wasn't included either — resolved by switching the prompt symbol to `:` instead of adding another single-character bitmap.

---

## 3. Building the Shell

Implemented a proper terminal-style interface rather than one-off text drawing:
- A scrolling output-line buffer (18 lines), with older lines dropping off automatically once full — genuine scrolling behavior, not just repeated overwriting
- A fixed prompt region, redrawn independently from the output area so typing never corrupts command history
- Commands implemented: `ls` (lists files via the filesystem), `cat <file>` (reads and displays file contents), `write <file>` (enters a text-input mode, saves what's typed to a new file), `ps` (lists running processes by PID), `help`, `clear`, and `capdemo` (launches the two capability-security demo processes on demand)

Removed the old boot-time debug drawings (the large test square, hex diagnostic printouts, the keyboard flash box) from `kernel_post_paging`, since the shell now serves as the primary, clean interface — boot proceeds silently into a working terminal instead of a screen full of accumulated test output.

---

## 4. Discovering a Real Process-Isolation Regression

While testing the new `capdemo` shell command, hit a page fault (`CR2 = 0x50000`). Traced the cause precisely:

- The physical memory manager's bitmap lives at a fixed low physical address, and multiple places in the kernel — page table creation, per-process PML4 creation — allocate a physical frame and then **directly dereference the returned physical address as a pointer**.
- This only works safely under the kernel's *own* original page tables, which include a low-half identity map covering all of physical RAM.
- Per-process page tables correctly **do not** share that low-half identity map (by design — that's genuine isolation), so any process other than the kernel faults immediately when this pattern is hit.

**An earlier quick fix** had patched this by making every process's page tables share PML4 entry 0 (the low-half identity map) with the kernel — which stopped the crash, but at the cost of giving every process direct read/write access to all physical RAM, including other processes' memory and the kernel's own internal structures. This silently defeated the isolation verified earlier.

**Root-caused a second time, more precisely:**
1. **Page table internals** (`get_or_create_table` in `paging.c`) needed access to a physical memory manager, or **the PMM's bitmap itself is inaccessible** under a process's isolated tables
2. Diagnosed specifically: `pmm_init()` stores the bitmap at a raw physical address, and `pmm_alloc_frame()` dereferences it directly — this call happens from *within* `process_create()` (to allocate a new PML4), meaning it runs under the *calling* process's page tables, not the kernel's

**The correct fix, applied properly this time:**
- Reverted the regression: per-process PML4s only copy entries 256-511 (the genuine higher half) from the kernel, entries 0-255 stay zeroed — true isolation restored
- Instead, physical RAM is **also** mapped at its HHDM (Higher Half Direct Map) address, which lands in the higher half and is therefore safely shared by every process by design
- Every place that previously dereferenced a raw physical address as a pointer (new page tables, the PMM bitmap, new per-process PML4s) was updated to go through this HHDM mapping instead
- Care was taken to keep CR3-loaded values as genuine physical addresses throughout — only the *dereferencing* changed, not what gets stored in `pml4_phys` or passed to `load_cr3`
- Avoided a duplicate Limine request panic (a known failure mode from earlier in the project) by having `pmm.c` reuse `paging.c`'s existing HHDM conversion function rather than declaring a second, independent HHDM request

**Verified:** `capdemo` runs without crashing, and the actual capability enforcement still works correctly — the process with a granted capability draws successfully, the process without one is still denied. All other shell commands (`ls`, `cat`, `ps`, `help`, `clear`) continued working correctly throughout.

---

## 5. Smaller Fixes Along the Way

- Fixed a fragile `ps` command that only correctly printed single-digit process IDs (`'0' + pid`, which produces garbage characters for PID ≥ 10) — replaced with a proper multi-digit number-to-string conversion
- Removed a duplicate function declaration in `process.h`

---

## 6. Reflection

The isolation regression is a good example of a quick fix solving the visible symptom while quietly compromising a deeper guarantee — the crash went away immediately with the first patch, but only a closer look revealed it had reopened exactly the security property verified earlier. Worth remembering as a general pattern: a fix that resolves a crash isn't automatically the *correct* fix, especially when the crash touches a security or isolation boundary.

---

## 7. Where Things Stand

- Boot foundation, memory management — complete
- Processes, scheduling, syscalls, per-process page-table isolation, capability-based security — complete and re-verified after the regression fix
- Disk driver and filesystem — complete and verified
- Interactive shell with real keyboard input, full font support, and working commands (`ls`, `cat`, `write`, `ps`, `help`, `clear`, `capdemo`) — complete

---

## 10. Next Steps

- [ ] Consider real ring 0 / ring 3 privilege separation for processes (currently all processes run at ring 0 — page-table isolation is real, but hardware privilege separation is not yet implemented)
- [ ] Improve `cat`/file output to handle multi-line content properly
- [ ] Add file deletion and a real free-space allocator for the filesystem (current version uses a fixed per-file-slot allocation)
- [ ] Consider a basic GUI layer built on top of the existing framebuffer and text rendering

---

*Log — Day 15, Infinity OS project.*
