# Infinity OS — Day 16

**Date:** September 13, 2026
**Focus:** Ring 0 / ring 3 privilege separation — GDT expansion, TSS, PAGE_USER, and the first working ring-3 process

---

Day 15 closed with ring 0/ring 3 separation flagged as a next step — every process ran at ring 0, with page-table isolation doing the real work of keeping processes apart, but no actual hardware privilege separation. Today's goal was to close that gap, broken into small, independently testable pieces rather than one large change.

---

## 1. GDT Expansion for User Privilege

The GDT only had 3 entries: null, kernel code, kernel data — no descriptors existed for ring 3 at all.

Added two more entries:
- User code: `0xFA` (same shape as kernel code's `0x9A`, with the DPL bits set to `11`)
- User data: `0xF2` (same relationship to kernel data's `0x92`)

Base/limit stay 0, matching the existing flat-segmentation model. This step deliberately changed nothing observable — no code loads these selectors yet — so "boots identically to before" was the actual pass condition, not a bug.

---

## 2. Building and Loading a Real TSS

Reasoned through *why* this is needed before building it: any ring 3 → ring 0 transition (an interrupt or syscall firing while a ring-3 process is running) forces the CPU to switch stacks automatically, and it needs to be told in advance which stack to switch to. That's what the TSS's `RSP0` field is for — not old-style hardware task switching, just a lookup the CPU consults on every privilege-raising transition.

- Added `tss.c`/`tss.h`: a real (mostly-unused in long mode) TSS struct, with `rsp0` as the field that matters right now
- The TSS descriptor itself is a different shape from code/data descriptors — 16 bytes instead of 8, since it needs a full 64-bit base address. Implemented by reinterpreting two adjacent GDT slots (indices 5 and 6) as one `struct tss_descriptor`, rather than writing them as two separate 8-byte entries
- GDT grew from 5 entries to 7 to make room for those two slots
- Added `tss_flush.asm` (`ltr`) to actually load it

**Verified, not assumed:** checked `info registers` in the QEMU monitor after boot. `TR = 0028` (selector = index 5 × 8, correct), base address matched `&g_tss` exactly as a full 64-bit higher-half address (proving the base-splitting bit math across four separate struct fields was all correct), limit `0x67` matched `sizeof(struct tss) - 1`, and QEMU explicitly decoded the type as `TSS64-avl`. Also noticed `GDT = ... 00000037` (0x37 = 7×8-1) confirming the whole 7-entry table was sized correctly — one register dump ended up validating several independent pieces of arithmetic at once.

`rsp0` is set to a single placeholder stack address for now (today's one shared kernel stack) — genuinely not safe yet for more than one ring-3 process taking interrupts concurrently. Flagged clearly as unfinished, not glossed over — see Next Steps.

---

## 3. PAGE_USER and a Subtlety in How x86-64 Actually Checks It

Added `PAGE_USER` (bit 2) to `paging.h`. The non-obvious part: the CPU checks the `US` bit at *every* level of the 4-level page walk (PML4 → PDPT → PD → PT), and the effective permission is the AND across all of them — not just whatever the final page's entry says.

This mattered directly: `get_or_create_table()` (which builds intermediate PML4/PDPT/PD entries on demand) had only ever set `PAGE_PRESENT | PAGE_WRITABLE` — never `PAGE_USER`. Reasoned through whether it's actually safe to set `PAGE_USER` unconditionally on every intermediate entry (it is — a directory entry having `US=1` doesn't grant anything by itself; the AND-across-levels rule means the leaf PTE is still the only place real per-page access control lives). Updated `get_or_create_table()` accordingly. Another "boots identically" step — no page is actually marked user-accessible yet.

---

## 4. The First Real Ring-3 Process

This tied the previous three pieces together and needed one genuinely new piece of infrastructure plus a careful rebuild of how a process gets launched.

**Gap found:** `paging_map()` only ever wrote into the kernel's own global PML4 — it had no way to map a page into a *different* process's tables, which is a problem when that process's PML4 isn't loaded into CR3 yet (and shouldn't be, mid-setup). Added `paging_map_into()`, which does the same table walk but starts from an explicitly-given PML4 physical address (via its HHDM view) instead of the global one.

**`process.h`** gained `kstack_top` and `ustack_top` — a ring-3 process needs a private kernel-entry stack *and* a separate user-mode stack, not the single stack every ring-0 process has used until now.

**`process_create_user()`** (new, alongside the existing `process_create()` rather than replacing it):
- Allocates a private kernel stack and a fresh per-process PML4, same as before
- Writes a small hand-assembled test payload (`mov eax, 0` ; `int 0x80` ; `jmp $` — 9 bytes) into a fresh physical frame via its HHDM view, then maps that same frame `PAGE_USER` into the new process's *own* tables at a fixed test address
- Maps a second page, `PAGE_WRITABLE | PAGE_USER`, as that process's user stack
- Builds the same kind of fake `iretq` frame `process_create()` already builds — but this time `CS`/`SS` are real user selectors with RPL=3 (`0x1B`/`0x23`, not `0x08`/`0x10`), and `RSP` is the real user stack address. Confirmed first that x86-64's `iretq` only pops all 5 frame values (including RSP/SS) on an actual privilege drop — every prior process launch was ring0→ring0, so those two fields had been sitting there completely inert until now

No changes were needed to `context_switch.asm` or `irq0_stub` — both already did a generic pop-registers-then-`iretq` restore, and that turned out to already handle a ring-3 landing correctly without modification, since `iretq`'s own behavior is what changes, not the code around it.

Also checked `idt.c`'s syscall gate (`int 0x80`) rather than assuming — it was already set to DPL=3 from earlier work, so ring-3 code invoking it wasn't blocked at the gate. One less thing to fix.

**Verification:** added a `ring3demo` shell command. First run produced two independent, stackable pieces of evidence:
1. The shell kept working normally for several commands *after* `ring3demo` ran (`ls`, `capdemo`, `ps` all produced correct output). Any GPF or page fault fills the entire screen with a solid color and halts forever (`isr_screen_halt`) — so continued normal operation is direct proof nothing in the new process ever faulted.
2. `keyboard_flash()` drew its signature 20×20 square in the corner, in green — its very first call always paints green (a static toggle), so this is direct evidence the syscall actually fired: ring-3 fetch from the `PAGE_USER` page succeeded, `int 0x80` was accepted, `syscall_handler` ran, and execution returned to ring 3 without fault.

Attempted to catch the process live in `CPL=3` via manual `info registers` polling over the QEMU monitor telnet socket — missed it (caught `CS=0008`/`CPL=0` instead), which makes sense in hindsight: the timer runs at 100 Hz, so each process gets roughly a 10ms slice, and manually timing a keystroke to land inside one specific 10ms window isn't realistic. Noted a better approach for next time: QEMU's GDB stub (`-s -S`) with a hardware breakpoint at the exact test code address, which halts deterministically the instant that address is reached rather than relying on timing luck.

---

## 5. Reflection

The most useful moment today wasn't writing new code — it was re-deriving the exact byte layout `process_create()`'s fake stack frame produces, field by field, and checking it against what `context_switch.asm`'s pop sequence actually expects, before writing a single line of the ring-3 version. That confirmed two things in advance instead of by trial and error: that the existing assembly needed zero changes, and that `iretq`'s pop-3-vs-pop-5 behavior was the actual mechanism making the RSP/SS fields meaningful for the first time. Tracing the exact mechanism before extending it caught what would otherwise have been a very confusing crash (or worse, silent success by accident) to debug after the fact.

---

## 6. Where Things Stand

- Boot foundation, memory management — complete
- Processes, scheduling, syscalls, per-process page-table isolation, capability-based security — complete
- Disk driver and filesystem — complete
- Interactive shell — complete
- GDT expanded for ring 3, TSS built and loaded and verified, `PAGE_USER` added and verified safe — complete
- First ring-3 process launched — strong indirect evidence of success (crash-free execution + syscall round-trip signal); direct `CPL=3` confirmation still pending

---

## 7. Next Steps

- [ ] Confirm ring-3 execution directly via a QEMU GDB breakpoint at the test code address, checking `CS`/`CPL` at that exact instruction
- [ ] Sync `TSS.rsp0` to the current process's own kernel stack on every context switch — required before more than one ring-3 process can safely take interrupts/syscalls concurrently
- [ ] Improve `cat`/file output to handle multi-line content properly
- [ ] Add file deletion and a real free-space allocator for the filesystem (current version uses a fixed per-file-slot allocation)
- [ ] Consider a basic GUI layer built on top of the existing framebuffer and text rendering

---

*Log — Day 16, Infinity OS project.*
