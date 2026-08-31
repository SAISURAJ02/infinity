# Infinity OS — Day 12

**Date:** August 31, 2026
**Focus:** Resolving the interrupt regression bug from Day 11, and confirming genuine continuous preemptive multitasking for the first time

---

## 1. Recap / Starting Point

Picking up directly from Day 11's unresolved bug: both the timer (IRQ0) and keyboard (IRQ1) interrupts fired exactly once, ever, then went completely silent — no crash, no fault logged. Three theories (PIT misconfiguration, PS/2 controller not enabled, indicator visibility) had all been tested and disproven with real debug-log evidence the night before. The plan was a fresh, systematic re-read rather than more guessing.

---

## 2. Root Cause Found

A careful, complete re-read of every file in the interrupt/scheduling chain (`idt.c`, `irq.asm`, `pic.c`, `process.c`, `kernel.c`, and supporting files) surfaced the actual bug — in `schedule()`, in `process.c`:

```c
uint64_t schedule(uint64_t current_rsp) {
    if (current_process != NULL) { ... }

    if (process_list == NULL) {
        return current_rsp; // <-- EARLY RETURN, before EOI
    }

    current_process = ...;
    current_process->state = PROCESS_RUNNING;

    pic_send_eoi(0); // only reached if process_list is non-NULL
    return current_process->rsp;
}
```

**The bug:** if `process_list` was `NULL`, the function returned **before ever calling `pic_send_eoi(0)`**. The PIC's End-of-Interrupt acknowledgment is not optional — without it, the 8259 PIC's internal in-service register stays permanently marked busy for that IRQ line, and the hardware will never deliver another interrupt on it (or, in this case, effectively any interrupt on the shared master PIC) until EOI is received.

**Why this exactly matched the symptoms:**
- `kernel_post_paging()` calls `sti` (enabling interrupts) *before* calling `process_create()` twice — there's real work in between (heap init, drawing text and the green square) during which the very first 100Hz timer tick could easily fire while `process_list` was still `NULL`.
- When that happened, `schedule()` hit the early return, **never sent EOI**, and the PIC's interrupt delivery froze permanently — explaining why *both* IRQ0 and IRQ1 died together (they share the same master PIC), and why the freeze was total and silent (no fault, since nothing crashed — the PIC just correctly stopped delivering interrupts as designed, given it never got acknowledged).
- This also explained the Day 11 bisection finding precisely: this bug could only exist in `schedule()`, introduced on Day 10 with preemptive scheduling. The previous handler (`irq_handler()`) called `pic_send_eoi()` unconditionally, with no early-return path — consistent with interrupts never showing this symptom before Day 10.

**Why last night's three fix attempts couldn't have worked:** none of them touched EOI logic at all — PIT configuration affects tick *generation*, PS/2 controller setup affects keyboard *generation*, and indicator visibility affects nothing at the hardware level. The actual bug was about interrupt *acknowledgment*, a completely different part of the pipeline none of those theories addressed.

---

## 3. The Fix

Moved `pic_send_eoi(0)` to unconditionally execute as the very first line of `schedule()`, before any early-return path:

```c
uint64_t schedule(uint64_t current_rsp) {
    pic_send_eoi(0); // ALWAYS acknowledge first, regardless of scheduling state
    ...
}
```

---

## 4. Verification

Tested with QEMU's CPU-level debug logging, same methodology used throughout the project:
```bash
qemu-system-x86_64 -cdrom infinity.iso -m 512M -d int,cpu_reset -D qemu_debug.log --no-reboot --no-shutdown
```
Ran for ~15-20 seconds, pressing several keys along the way.

**Result:**
```
grep -c "v=20" qemu_debug.log  → 1258   (timer interrupts)
grep -c "v=21" qemu_debug.log  → 11     (keyboard interrupts)
```

This is the first time in the project's history that genuine, continuous, repeating interrupt-driven preemption has been confirmed with hard data — not visual impression. The Day 10 "success" was almost certainly a single switch event with static leftover pixels, not real ongoing alternation, as suspected during Day 11's bisection.

---

## 5. Committed

```
git commit -m "Fix critical scheduler bug: schedule() skipped pic_send_eoi() on its early-return path when no processes existed yet, permanently stalling the PIC's in-service register and silently blocking ALL future interrupts (timer and keyboard). Moved EOI to always fire first. Verified via debug log: 1258 timer ticks / 11 keyboard events over ~15s, confirming genuine continuous preemptive multitasking for the first time. Also includes pit_init() and keyboard_init() additions and per-process PML4 creation (create_process_pml4) from prior investigation - neither was the actual root cause but both are correct, valid additions."
```

This single commit consolidates: the actual fix, plus Day 11's legitimate (if not-the-culprit) additions — `pit_init()`, `keyboard_init()`, and `create_process_pml4()` — all of which are correct, useful code in their own right, now built on top of a genuinely solid interrupt foundation.

---

## 6. Reflection

This closes out one of the more instructive debugging episodes in the project. The discipline of rejecting each plausible-but-unverified fix based on hard log evidence — rather than accepting a fix because it *sounded* reasonable — was exactly what led to finding the real cause the next morning with a clear head, on the first careful pass. The bug itself is a good, generalizable lesson: **any early-return path in an interrupt handler must be checked to ensure hardware acknowledgment (EOI) still happens** — a class of bug that's easy to introduce when adding new logic (like scheduling decisions) into a handler that used to be simpler and unconditional.

---

## 7. Where we Stands

- ✅ Process creation (PCB, fake stack frames)
- ✅ Preemptive scheduling — now **genuinely verified working continuously**, not just apparently
- ✅ Syscalls (ring-3-triggerable, proven round trip)
- ✅ Per-process PML4 creation (higher-half copied from kernel, lower-half empty)
- ⬜ Actual CR3 switching during context switches — `pml4_phys` is created and stored, but not yet used; every process still executes under the kernel's shared page tables
- ⬜ Capability-based security — the signature feature, still pending the above

---

## 8. Wiring Real Per-Process CR3 Switching

With the interrupt foundation confirmed solid, moved directly into making `pml4_phys` actually used, not just stored.

**Design choice:** rather than threading CR3-switching through `context_switch.asm`'s existing register-passing convention (adding risk to an already-delicate assembly routine), added a small, separate C-callable helper:
```nasm
global load_cr3
load_cr3:
    mov cr3, rdi
    ret
```
Called directly from C, from both switch paths — `scheduler_run_next()` (first switch) and `schedule()` (timer-driven switches) — right before the existing register/stack-switching logic runs. Reasoning: since every process's higher-half mappings are identical to the kernel's (from Day 11's `create_process_pml4()`), switching CR3 while still executing under the *current* correctly-mapped stack is safe — the actual jump to different code/stack only happens afterward, via `context_switch`/`iretq`.

**Result: worked correctly on the first attempt.** Verified with the debug log:
```
grep -c "v=20" qemu_debug.log  → 2870
grep -c "v=21" qemu_debug.log  → 47
```
No faults anywhere in the log. Critically, inspected two consecutive timer-tick events directly in the log and confirmed **CR3 genuinely alternates between two distinct physical addresses** (`0x1d6000` and `0x1d5000`) tick to tick — direct, hard proof that per-process address space switching is real and working, not just code that compiles and doesn't crash.

Committed:
```
git commit -m "Wire per-process CR3 switching into scheduler_run_next() and schedule(): added load_cr3() helper, called before every context switch (first switch and timer-driven). Verified via debug log - CR3 genuinely alternates between distinct physical addresses across consecutive timer ticks (0x1d6000 / 0x1d5000), confirming real per-process address space isolation for the first time. 2870 timer ticks / 47 keyboard events logged with zero faults."
```

---

## 9. Core Mechanics Complete

- ✅ Process creation (PCB, fake stack frames)
- ✅ Preemptive scheduling — verified continuous, EOI bug fixed
- ✅ Syscalls (ring-3-triggerable, proven round trip)
- ✅ Per-process page tables, with **verified** CR3 switching between distinct address spaces
- ⬜ Capability-based security — the signature feature, now unblocked

Every mechanical piece the capability system depends on is now genuinely in place: isolated processes, a controlled syscall gateway, and confirmed hardware-level separation between them.

---

## 10. Starting Capability-Based Security

With placement prep coming up next, agreed to scope this deliberately: not a full seL4-style formal system, but a **correct, genuine, demonstrable core** — enough to explain and show confidently in an interview, not an exhaustive implementation.

**Design outlined:**
1. Each process gets a small, fixed-size capability table (an array added to `struct process`)
2. Each capability entry: a resource type, a resource identifier, and a permission flag
3. **The syscall dispatcher becomes the enforcement point** — before performing a privileged operation, it checks whether the calling process's capability table actually grants it; if not, deny cleanly and visibly (not just silently fail)
4. **The planned demo:** two test processes, one *with* a capability for a gated resource and one *without* — visibly succeeding vs. being cleanly denied, told as a concrete, working story rather than an abstract claim

Open question at end of session: whether to gate the existing `SYS_TEST` syscall (simplest, reuses what's already built) or add one more clearly "privileged" resource first, to make the demo read more obviously like a real security feature — to be decided at the start of next session.

---

## 11. Next Steps

- [ ] Decide which resource(s) the first capability check will gate
- [ ] Add a capability table to `struct process`
- [ ] Implement the enforcement check inside `syscall_handler`
- [ ] Build the two-process demo (with capability vs. without) and verify the denial is genuinely enforced, not just a UI difference
- [ ] Document the capability model clearly (README/design doc) — this is the project's signature feature and needs to be explainable cleanly for interviews
- [ ] Once solid: shift primary focus toward placement prep, treating remaining Infinity work (next : filesystem, shell, GUI) as ongoing/best-effort rather than a hard deadline

---

*Log — Day 12, Infinity OS project.*
