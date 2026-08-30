# Infinity OS — Day 11

**Date:** August 30, 2026
**Focus:** Per-process address space isolation (PML4 creation), and a genuinely difficult, still-unresolved interrupt bug

---

## 1. Recap / Starting Point

Continuing from the syscall milestone. Today's planned work: give each process its own page tables instead of sharing the kernel's, as the next step toward true process isolation and eventually capability-based security.

---

## 2. Per-Process PML4 Creation

Worked through the design question from the previous session: since the kernel (interrupts, syscalls, scheduler) must keep functioning regardless of which process's address space is active, established that **the entire higher half of every process's PML4 must be identical to the kernel's own** — correctly reasoned independently, then confirmed precisely: this corresponds exactly to PML4 indices 256-511, since that's where the top address bit (making an address "higher-half canonical") lands once run through the existing `pml4_index()` formula.

**Implemented `create_process_pml4()` in `process.c`:**
- Allocates a fresh frame via `pmm_alloc_frame()` for the new PML4
- Copies entries 256-511 directly from the kernel's existing PML4 (retrieved via `paging_get_pml4()`)
- Leaves entries 0-255 (lower half, user-space) empty for now — to be populated once processes get real user-space memory

Wired this into `process_create()`, replacing the old `proc->pml4_phys = 0` placeholder.

**Compiled and ran as an intermediate checkpoint** (deliberately not yet wiring actual CR3 switching into `context_switch`/`irq0_stub` — just proving PML4 creation itself doesn't crash anything). Build succeeded; visible behavior (boxes, text) appeared unchanged, as expected for this checkpoint.

---

## 3. Discovered: A Serious, Pre-Existing Interrupt Bug

While checking the keyboard-flash indicator as a basic sanity check after the PML4 checkpoint, noticed it had stopped responding to keypresses. Investigated properly using QEMU's CPU-level debug logging rather than relying on visual observation.

**Core finding:** using `grep -c "v=20" qemu_debug.log` (timer, IRQ0) and `grep -c "v=21" qemu_debug.log` (keyboard, IRQ1) as ground-truth interrupt counters, discovered that **both interrupt sources fire exactly once, ever, then go completely silent** — no crash, no fault logged, just silence. This is a fundamentally different failure mode from every previous bug in the project (no triple fault, no page fault, nothing to trace via a fault stack).

**Critical, honest discovery via bisection:** checked out yesterday's "successful preemptive multitasking" commit (`3344c4d`) directly and ran the identical measurement — **it showed the exact same result (1 tick, then silence).** This means yesterday's apparent success (both blue and magenta boxes visible) was very likely never genuine continuous alternating — it was a single switch event, with both processes' already-drawn pixels sitting statically on screen, misread as active alternation. The project may never have had genuinely repeating timer-driven preemption, only a single working switch, since Day 10.

### Theories tested and disproven with hard data tonight:
1. **PIT never configured to repeat** — added explicit `pit_init(100)` (correct PIT command byte, mode 3 square wave, proper frequency divisor). Rebuilt, retested: tick count unchanged (still 1, 0).
2. **8042 PS/2 controller interrupt generation disabled by Limine** — added a full `keyboard_init()` routine (drain buffer, enable port, read/modify/write the controller configuration byte to enable IRQ1, enable scanning). Rebuilt, retested: tick count unchanged (still 1, 0). Additionally, this theory was logically inconsistent with the symptom from the start — it could only explain keyboard (IRQ1) failing, never the timer (IRQ0), which shares no hardware path with the PS/2 controller. Confirmed and rejected on that basis even before testing, then disproven empirically as well.
3. **Indicator visibility/QEMU focus capture** — a suggested explanation proposing the interrupt was firing but the visual result was too subtle to see. Rejected immediately as inconsistent with the actual evidence: the diagnostic in use (`grep -c` on real logged CPU interrupt events) is entirely independent of pixels drawn or window focus — a logged count of 0 cannot be explained by a visibility problem.

**Code traced by hand and found structurally correct** (does not explain the bug, but rules out a class of causes): `irq0_stub`'s push/pop/EOI sequence, `schedule()`'s logic, `process_create()`'s fake-frame layout (RFLAGS has the interrupt-enable bit set, `0x202`), `pic_remap()`, and `idt_init()`'s entry registration all checked out correctly against each other when cross-referenced together.

---

## 4. Decision: Stop Guessing, Plan a Proper Bisection

After three consecutive disproven theories, made a deliberate call not to keep applying further speculative fixes late in the session. Discussed directly: the goal of resolving bugs same-day is a good instinct in general, but not at the cost of guessing while fatigued — continuing to try plausible-sounding fixes without first re-establishing solid ground risks either missing the real cause again or, worse, masking the symptom with an incorrect "fix" that provides false confidence heading into more complex work (per-process isolation, capabilities).

**Agreed plan for next session:** a proper systematic bisection — check out earlier commits (Day 5's original PIC/keyboard implementation, before preemptive scheduling existed at all) and run the identical `grep -c` measurement against each, to determine precisely when (or whether) continuous, repeating interrupts ever actually worked in this project, rather than continuing to guess at fixes for a bug whose true origin point isn't yet confirmed.

---

## 5. Current State (uncommitted, nothing lost)

- `process.c` — `create_process_pml4()` and its wiring into `process_create()`, working correctly as far as it's been tested (does not yet switch CR3)
- `pic.c`/`pic.h`/`kernel.c` — `pit_init(100)` and `keyboard_init()` additions from tonight's (disproven) fix attempts; left in place since they're reasonable, correct code in their own right, even though they didn't resolve tonight's actual bug
- All changes remain **uncommitted** in the working directory — deliberately not committed, since the underlying interrupt-repetition bug means the current state should not be treated as a new stable checkpoint

---

## 6. Reflection

Tonight was a genuinely difficult session, and the most useful outcome wasn't a fix — it was ruling out three plausible causes with real evidence, and discovering (via disciplined bisection against yesterday's commit) that this bug likely predates today's work entirely. That's a meaningfully different, more accurate understanding of the project's actual state than believing yesterday's multitasking milestone was fully solid. Better to know this now, before building further process-isolation work on an assumption that repeating preemption was ever truly working.

---

## 7. Next Steps

- [ ] Systematic bisection: test `grep -c "v=20"` against Day 5's commit (original PIC/keyboard work, pre-scheduling) to establish whether repeating interrupts ever worked at all in this project
- [ ] If found working at some historical point, narrow down which specific commit introduced the "only fires once" regression
- [ ] Once root cause is identified with confidence (not guessed), apply and verify the actual fix
- [ ] Only then resume: wiring CR3 switching into `context_switch.asm`/`irq0_stub` so `pml4_phys` is actually used during context switches
- [ ] Continue toward capability-based security once isolation is genuinely solid and verified

---

*Log — Day 11, Infinity OS project.*
