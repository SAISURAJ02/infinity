# Infinity OS — Day 17

**Date:** September 13, 2026
**Focus:** Pillar 5 — syncing TSS.rsp0 per process, closing out ring 0/ring 3 privilege separation, and two real bugs found along the way

---

Day 16 got a ring-3 process running for the first time, but left one thing explicitly unfinished: `TSS.rsp0` was still a single hardcoded placeholder shared by every process — fine with exactly one ring-3 process, unsafe the moment a second one exists. Today closed that gap.

---

## 1. Syncing RSP0 at Every Switch Point

Worked out first that the scheduler actually has *two* separate places where the running process changes, not one: `scheduler_run_next()` (runs exactly once, at boot, to launch the very first process) and `schedule()` (the recurring per-tick switch, called from `irq0_stub`). Both needed the fix — the very first process ever launched could itself be a ring-3 process and take a syscall or get preempted before `schedule()` has run even once, so relying on only the recurring path would leave a window open right at boot.

Added `tss_set_rsp0(current_process->kstack_top)` right after `current_process` changes, in both branches of `scheduler_run_next()` and inside `schedule()`.

Also cleaned up an implicit assumption: `process_create()` (the original ring-0-only path) had never explicitly set `kstack_top`/`ustack_top` — left as uninitialized heap garbage. Harmless in practice (a ring-0 process never triggers a privilege-raising RSP0 read), but fragile to leave undocumented. Now explicitly sets `kstack_top` to its own stack and `ustack_top` to 0.

---

## 2. Bug Found: Silent Heap Exhaustion

Testing this surfaced a real, independent bug — not caused by today's change, but exposed by it. Ran `capdemo` (2 processes) then `ring3demo` twice, and `ps` showed the exact same three PIDs both times, with no new process ever appearing.

Traced it to actual numbers rather than guessing: the heap (`heap.c`) was only 64KB total (`HEAP_INITIAL_PAGES = 16`). Each process — struct plus its 16KB kernel stack — costs roughly 16.6KB. Shell + `capdemo`'s two processes had already consumed ~50KB before `ring3demo` ever ran, leaving ~15.5KB — not enough for a 4th process's stack. `kmalloc()` correctly returned `NULL`, and `process_create_user()` correctly bailed out — the allocator did the right thing, it just didn't have enough to work with.

This also retroactively explained why the *very first* ring-3 test (Day 16) worked fine: that session happened to run `ring3demo` *before* `capdemo`, so far less heap had been consumed at that point. Same mechanism, different command order, different outcome — a good reminder that "it worked once" isn't the same as "it's correct."

**Fix:** bumped `HEAP_INITIAL_PAGES` to 64 (256KB) — real headroom instead of a bare minimum.

---

## 3. Bug Found: Silent Failure Reporting in the Shell

The `ring3demo` shell command never checked `process_create_user()`'s return value — it printed a success message unconditionally, even when creation had just failed. This is exactly why the heap exhaustion above wasn't obvious from the shell's own output; `ps` was the only thing telling the truth. Fixed to check for `NULL` and report failure explicitly instead of assuming success.

---

## 4. Direct Confirmation, Not Inferred

After the heap fix, `ps` correctly showed growing PIDs (up to PID 9) across repeated `ring3demo`/`capdemo` calls — proof creation was succeeding properly now.

Caught a `ring3demo` process live via the QEMU monitor's `info registers`:
- `CPL=3`
- `CS=001b`, `SS=0023` — both shown with `DPL=3`
- `RIP=0000000000400007`

That last one is the detail that removes any doubt: the test payload is `mov eax,0` (offsets 0-4) → `int 0x80` (offsets 5-6) → `jmp $` (offset 7). Offset 7 is exactly where this snapshot caught it — meaning the syscall round-trip through ring 0 and back had already completed, and the process was sitting in its intended infinite spin. No other code anywhere in the kernel ever executes at that address, so this is unambiguous, not circumstantial.

---

## 5. Reflection

Both bugs today were found the same way: by trusting a concrete, checkable fact (`ps`'s actual process list) over a message that merely claimed success. The shell's own "Ring 3 process launched" text was wrong twice in a row, and would have kept being wrong indefinitely if `ps` hadn't been checked as a second, independent source of truth. Worth keeping as a general habit — a success message is a claim, not evidence, and the two aren't always the same thing.

---

## 6. Where Things Stand

- Boot foundation, memory management — complete
- Processes, scheduling, syscalls, per-process page-table isolation, capability-based security — complete
- Disk driver and filesystem — complete
- Interactive shell — complete
- Full ring 0 / ring 3 privilege separation (all 5 pillars: GDT, TSS, PAGE_USER, ring-3 process launch, RSP0 sync) — complete, with direct register-level confirmation (`CPL=3`, correct `CS`/`SS`, `RIP` landing exactly on the expected instruction)
- Heap size and a silent-failure gap in the shell, both found and fixed along the way

---

## 7. Next Steps

- [ ] Load real (non-hardcoded) user code into a ring-3 process — even a small custom binary format short of a full ELF loader
- [ ] Improve `cat`/file output to handle multi-line content properly
- [ ] Add a real bounds check on file size in `fs_write_file` (currently a file over ~32KB would silently overwrite the next directory slot) and a real free-space allocator
- [ ] Consider a basic GUI layer built on top of the existing framebuffer and text rendering

---

*Log — Day 17, Infinity OS project.*
