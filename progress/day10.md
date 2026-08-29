# Infinity OS — Day 10

**Date:** August 29, 2026
**Focus:** Syscall mechanism (int 0x80), honest scope discussion on OS ambitions, starting per-process address space isolation

---

## 1. Recap / Starting Point

Continuing directly from Day 9's preemptive multitasking milestone. Today's goal: build a syscall mechanism — the controlled interface processes use to request kernel services — as the next piece toward Month 3's eventual capability-based security feature.

---

## 2. Scope Discussion: What "A Proper OS" Actually Means

Before returning to code, had an important, honest planning conversation. The ambition expanded to include a full desktop-style experience: proper UI, triple-boot-ready ISO, terminal, settings, built-in applications, and — significantly — a built-in browser (initially proposed as porting Firefox/Chrome) plus general third-party application installation support.

**Researched current OS/security research context first** (scheduler design trends, capability-based security lineage) to confirm the planned security feature is well-grounded (seL4, KeyKOS/EROS lineage) without chasing cutting-edge complexity beyond project scope.

**Gave an honest, direct assessment on the browser/app-install ambition:**
- Confirmed: bootable ISO, triple-boot capability, a real terminal, basic settings, and a couple of simple built-in applications are all genuinely achievable within the placement timeline — these remain in scope.
- Explained clearly why porting an existing browser (Firefox/Chrome) is not realistic: these assume a mature, POSIX-compatible OS underneath (full libc, dynamic linking, a complete TCP/IP + TLS networking stack, multi-threaded synchronization primitives, GPU driver support) — none of which currently exist in Infinity, and building all of it is itself a separate, massive undertaking.
- Cited concrete precedent: even long-running, multi-contributor hobby OS projects (Haiku, Redox, SerenityOS) do not run Chrome/Firefox; SerenityOS's own browser (Ladybird) took years of dedicated team effort. The realistic reference point for a hobby OS is something like NetSurf, and even that requires a working libc/networking stack first.
- **Agreed outcome:** keep browser support and general app installation as an explicit "future roadmap" item, positioned honestly as post-placement, indefinite-timeline work — not part of the current 4-month deliverable. Decided approach: build a genuinely solid, working core within the placement window, then continue developing Infinity indefinitely afterward without deadline pressure, following the same evolution path as other long-running hobby OS projects.

---

## 3. Syscall Mechanism — Concept

Established the core idea: a syscall is a **deliberate, user-triggerable interrupt** (`int 0x80`, x86 convention) — mechanically similar to everything already built (IDT entry, assembly stub, C handler), but with one critical difference: it must be triggerable by ring-3 (user-mode) code, unlike every other existing IDT entry.

**Worked through the precise mechanism carefully, correcting an initial misconception:** the syscall *handler code* still runs at ring 0 (full kernel privilege) — what changes is the **IDT entry's DPL (Descriptor Privilege Level) field**, which governs *who is allowed to trigger this specific entry via software `int`*, separate from what privilege level the handler executes at once entered.

Decoded the exact bit meaning: existing entries use `type_attr = 0x8E` (binary `10001110`) — DPL bits `00`, kernel-only. The new syscall entry uses `type_attr = 0xEE` (binary `11101110`) — DPL bits `11` (ring 3), explicitly permitting user-mode code to trigger only this one entry, while every other entry (exceptions, hardware interrupts) remains ring-0-only, as it must — a process should never be able to fake a page fault or timer interrupt directly.

---

## 4. Implementation

- **`syscall_entry.asm`** (renamed from an initial `syscall.asm` to avoid an object-file naming collision with `syscall.c`) — assembly stub following the established save-registers → call C handler → restore-registers → `iretq` pattern, without the dummy error-code push used by IRQ stubs (software-triggered interrupts via `int` don't need it here).
- **`syscall.c`** — `syscall_handler()`, reading the syscall number from the saved `rax`, dispatching via a `switch`, and writing a return value back into the same saved `rax` slot so the calling process sees the result after the stub pops registers back.
- **`syscall.h`** — declared `syscall_handler`; hit and fixed a type-mismatch compile error (header declared `void *regs`, implementation used `struct registers *regs`) by adding a forward declaration (`struct registers;`) and matching the real parameter type exactly.
- **IDT wiring** — added `idt_set_entry(0x80, (uint64_t)syscall_entry, 0x08, 0xEE)` in `idt_init()`, the one and only DPL=3 entry in the whole table.
- **Calling convention test** — added `do_syscall(uint64_t syscall_number)` in `kernel.c`, an inline-assembly helper that loads the syscall number into `rax`, executes `int $0x80`, and reads the result back from `rax`. Called once from `test_process_1` (syscall 0, `SYS_TEST`), which triggers `keyboard_flash()` as a visible, unambiguous proof the full round trip works — process → interrupt → kernel dispatcher → real action → return value → back to process.

**Result: confirmed working on first build after the type-mismatch fix** — no crashes, syscall pipeline proven end-to-end.

Committed:
```
git commit -m "Implement first syscall mechanism: int 0x80 with ring-3-accessible IDT entry (DPL=3), syscall dispatcher reading rax for syscall number, test syscall proven working end-to-end from a user process"
```

---

## 5. Reflection

This was a comparatively smooth implementation relative to yesterday's multi-bug preemption debugging — a sign that the underlying interrupt/stack mechanics are now genuinely well understood and reusable, rather than each new interrupt-based feature requiring fresh first-principles debugging. The one real bug (header/implementation type mismatch) was a simple, quickly diagnosed compile-time issue, not a runtime mystery.

Architecturally, this syscall dispatcher is specifically where the planned capability-based security checks will eventually live — every privileged operation a process requests will be validated here before being performed, once capabilities exist.

---

## 6. Where Month 3 Stands

- ✅ Process creation (PCB, fake stack frames)
- ✅ Preemptive scheduling (timer-driven, concurrent execution confirmed)
- ✅ Syscalls (ring-3-triggerable, proven round trip)
- ⬜ Per-process address space isolation — `pml4_phys` still unused; all processes currently share the kernel's page tables with zero memory isolation between them
- ⬜ Capability-based security — depends on the above being in place first

---

## 7. Started: Per-Process Address Space Isolation

Began the next piece — giving each process its own PML4 instead of sharing the kernel's page tables, using the same `pmm_alloc_frame()`/`paging_map()` machinery already built in Month 2.

**Open design question raised, not yet resolved:** since the kernel itself must remain fully functional (interrupts, syscalls, scheduler) regardless of which process's address space is currently active, some regions (kernel code/data, IDT, GDT, etc.) must be identically mapped into *every* process's page tables without exception — to be worked through in detail next session.

---

## 8. Next Steps

- [ ] Resolve what must be identically mapped into every process's address space (kernel regions that can never become unmapped)
- [ ] Implement per-process PML4 creation during `process_create()`
- [ ] Switch CR3 as part of context switching (extending the existing scheduler/context-switch logic)
- [ ] Test carefully — this class of change (switching page tables per-process) carries the same real crash risk profile as the original CR3 switch work from Month 2, so plan for careful, incremental testing with debug logging
- [ ] Once isolation is solid: begin concrete design of the capability-based security model

---

*Log — Day 10, Infinity OS project.*
