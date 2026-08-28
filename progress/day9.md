# Infinity OS — Day 9

**Date:** August 28, 2026
**Focus:** Process creation, first context switch, and real preemptive multitasking — plus a genuinely intense debugging arc

---

## 1. Recap / Starting Point

Following up on text rendering (bitmap font) and a small code-quality fix (implicit function declaration warning), moved into the core of the roadmap's next phase: processes and scheduling.

Also did a brief, honest research check — searched current OS research (scheduler design, capability-based security) to confirm Infinity's planned signature feature (capabilities) is well-grounded in real, respected systems (seL4, KeyKOS/EROS lineage), while deliberately deciding not to chase cutting-edge research complexity (userspace schedulers, formal verification) given the project's realistic scope and timeline.

---

## 2. Designing the Process Control Block (PCB)

Discussed what a process fundamentally needs at the OS level: its own address space, its own stack, saved register state, and bookkeeping (PID, status). Landed on a `struct process` with `pid`, `state` (READY/RUNNING/BLOCKED), `rsp` (saved stack pointer — deliberately just one field, not 15 separate register fields), `pml4_phys` (placeholder for future per-process address spaces), and `next` (linked list for the scheduler).

**Key design reasoning:** only `rsp` is stored directly in the struct; all other register state lives *on that process's own stack*, restored by popping — directly reusing the same save/restore pattern already built for interrupt handling (`isr_common_stub`), just applied at the process level instead of per-interrupt.

---

## 3. `process_create()` — the Fake Stack Frame Technique

Implemented `process_create()`: allocates a PCB and a dedicated 16KB stack via the heap allocator, then manually constructs a **fake saved-interrupt-frame** on that new stack — `ss`, `rsp`, `rflags`, `cs`, `rip` (set to the process's actual entry point), followed by 15 zeroed general-purpose registers — so that a process which has *never actually run* can be "resumed" using the exact same restore logic as a genuinely paused one.

---

## 4. `context_switch.asm` and the First Real Switch

Wrote the assembly context-switch routine: save all 15 GP registers of the currently running code, store the resulting `rsp` into the old process's PCB, load the new process's saved `rsp`, restore its 15 registers, and `iretq` into it.

Wrote a minimal round-robin `scheduler_run_next()` in C to walk the process list and trigger a switch.

Built two simple test processes (`test_process_1`, `test_process_2`) — infinite loops each drawing a small colored box — to visually prove process execution.

---

## 5. Debugging Arc — Three Real, Sequential Bugs

This was the substantial part of today's session. Each bug was found via QEMU's CPU-level debug logging (`-d int,cpu_reset -D qemu_debug.log --no-reboot --no-shutdown`), cross-checked with independent AI-assisted analysis of the same logs, and verified against the actual assembly/C before applying any fix.

### Bug 1 — RSP placeholder in the fake stack frame
**Symptom:** immediate triple fault on the very first attempt.
**Diagnosis:** the fake frame's `rsp` field was set to a placeholder `0`, based on an incorrect assumption that `iretq` only reloads RSP on a privilege-level change. In 64-bit long mode, `iretq` **unconditionally** reloads all 5 values (RIP, CS, RFLAGS, RSP, SS) — even for ring0→ring0. RSP was set to literal `0`, and the first push/pop after switching immediately faulted on address ~`0x0`.
**Fix:** store the process's genuine, valid stack-top address in that field instead of a placeholder.
**Result:** first switch into `test_process_1` succeeded — confirmed visually (blue test box rendered, no crash).

### Bug 2 — Missing dummy int_no/err_code causing a frame-layout mismatch
**Symptom:** after wiring the timer interrupt to trigger scheduling (`irq0_stub` → `schedule()`), only a single timer interrupt ever fired, then total silence — no crash, no further events logged.
**Diagnosis:** `irq0_stub`'s epilogue unconditionally does `add rsp, 16` before `iretq`, to skip the `int_no`/`err_code` values a *real* hardware interrupt entry pushes. But `process_create()`'s manually-built fake frame never included those two dummy values — so on the second (timer-driven) switch, that `add rsp, 16` skipped into the real `iretq` frame data itself, corrupting the pop sequence.
**Fix:** added two dummy pushes (`int_no = 32`, `err_code = 0`) to `process_create()`'s fake frame, matching exactly what `irq0`'s real entry pushes.

### Bug 3 — Missing stack cleanup in `context_switch.asm`
**Symptom:** after fixing Bug 2, a General Protection Fault (exception 13, error code `0x20`) occurred immediately after the first timer tick.
**Diagnosis:** decoded the GPF error code precisely — `0x20` corresponds to GDT selector index 4, which doesn't exist (Infinity's GDT only has 3 entries: null, kernel code, kernel data). Traced this to `context_switch.asm` — used for the very *first* switch via `scheduler_run_next()` — never having an `add rsp, 16` before its `iretq`, since it predated Bug 2's fix. Once the fake frame gained the two dummy values, `context_switch.asm` was now also reading a stack it didn't account for: `iretq` popped `err_code` (0) into RIP and `int_no` (32) into CS — and `32 / 8 = 4`, exactly matching the invalid selector index in the fault.
**Fix:** added the missing `add rsp, 16` to `context_switch.asm`, matching `irq0_stub`'s epilogue.

### Additional defensive fixes applied in the same pass
- **PMM: reserved physical frame 0** — `pmm_alloc_frame()` could otherwise return address `0x0`, which callers correctly (but dangerously) interpret as `NULL`/allocation failure.
- **Heap: 8-byte alignment in `kmalloc()`** — unaligned allocation sizes could place subsequent block headers at unaligned addresses, a latent correctness/performance issue on 64-bit access.

---

## 6. Success — Real Preemptive Multitasking Confirmed

After all three fixes: **both test processes render simultaneously** (blue and magenta boxes both visible), confirming the timer is genuinely interrupting and switching between two independently-running processes. Additionally confirmed the **keyboard interrupt (IRQ1) still works correctly**, interleaved with the timer-driven switching — pressing a key correctly toggles the existing indicator box, proving multiple interrupt sources coexist correctly under the new scheduling logic.

Committed:
```
git commit -m "Implement preemptive multitasking: timer-driven scheduler via irq0_stub, two processes running concurrently. Fixed 3 real bugs along the way..."
```

---

## 7. Reflection

This was genuinely the most complex debugging arc since the paging/CR3 work — three distinct, sequential bugs, each only fully understood by precisely decoding low-level CPU state (register dumps, GPF error codes as selector indices) rather than guessing. The GPF error-code decoding in particular (`0x20` → GDT index 4 → matching `int_no = 32 → 32/8 = 4`) is a good concrete example of connecting a raw hex value back to an exact, explainable root cause — the kind of reasoning worth being able to walk through in an interview.

---

## 8. Where Month 3 Stands

- ✅ Process creation (PCB, fake stack frames)
- ✅ Preemptive scheduling (timer-driven, real concurrent execution confirmed)
- ⬜ Syscalls — controlled interface for processes to request kernel services
- ⬜ Per-process address spaces — `pml4_phys` exists in the struct but is currently unused; all processes still share the kernel's page tables, so true memory isolation between processes is not yet implemented
- ⬜ Capability-based security — the signature feature, still dependent on syscalls and per-process isolation existing first

---

## 9. Next Steps

- [ ] Design and implement a syscall mechanism (likely via a dedicated interrupt vector, e.g. `int 0x80` or similar convention)
- [ ] Give each process its own page tables (building on the paging system from Month 2) for real memory isolation
- [ ] Begin designing the capability-based security model concretely, now that processes and (soon) isolated address spaces exist

---

*Log — Day 9, Infinity OS project.*
