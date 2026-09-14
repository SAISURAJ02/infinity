# Infinity OS — Day 18

**Date:** September 15, 2026
**Focus:** Pillar 5 (TSS.rsp0 sync, closing ring 0/ring 3 privilege separation), a filesystem size bounds-check bug fix, and full multi-line file content support

---

Day 17 got a ring-3 process running for the first time, but left one thing explicitly unfinished: `TSS.rsp0` was still a single hardcoded placeholder shared by every process — fine with exactly one ring-3 process, unsafe the moment a second one exists. Today closed that gap, then moved on to the next item on the roadmap: the filesystem.

---

## 1. Syncing RSP0 at Every Switch Point

Worked out first that the scheduler actually has *two* separate places where the running process changes, not one: `scheduler_run_next()` (runs exactly once, at boot, to launch the very first process) and `schedule()` (the recurring per-tick switch, called from `irq0_stub`). Both needed the fix — the very first process ever launched could itself be a ring-3 process and take a syscall or get preempted before `schedule()` has run even once, so relying on only the recurring path would leave a window open right at boot.

Added `tss_set_rsp0(current_process->kstack_top)` right after `current_process` changes, in both branches of `scheduler_run_next()` and inside `schedule()`.

Also cleaned up an implicit assumption: `process_create()` (the original ring-0-only path) had never explicitly set `kstack_top`/`ustack_top` — left as uninitialized heap garbage. Harmless in practice (a ring-0 process never triggers a privilege-raising RSP0 read), but fragile to leave undocumented. Now explicitly sets `kstack_top` to its own stack and `ustack_top` to 0.

---

## 2. Bug Found: Silent Heap Exhaustion

Testing this surfaced a real, independent bug — not caused by today's change, but exposed by it. Ran `capdemo` (2 processes) then `ring3demo` twice, and `ps` showed the exact same three PIDs both times, with no new process ever appearing.

Traced it to actual numbers rather than guessing: the heap (`heap.c`) was only 64KB total (`HEAP_INITIAL_PAGES = 16`). Each process — struct plus its 16KB kernel stack — costs roughly 16.6KB. Shell + `capdemo`'s two processes had already consumed ~50KB before `ring3demo` ever ran, leaving ~15.5KB — not enough for a 4th process's stack. `kmalloc()` correctly returned `NULL`, and `process_create_user()` correctly bailed out — the allocator did the right thing, it just didn't have enough to work with.

This also retroactively explained why the *very first* ring-3 test (Day 17) worked fine: that session happened to run `ring3demo` *before* `capdemo`, so far less heap had been consumed at that point. Same mechanism, different command order, different outcome — a good reminder that "it worked once" isn't the same as "it's correct."

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

This closed out all 5 pillars of ring 0/ring 3 privilege separation with direct register-level evidence, not just crash-free execution.

---

## 5. Bug Found and Fixed: Unbounded File Size in fs_write_file

Moving to the next roadmap item (filesystem), the first thing addressed was a bug flagged earlier but not yet fixed: `fs_write_file()` never checked `size` against the fixed 64-sector (32KB) per-file slot. A file larger than that would silently overwrite the next directory slot's data on disk — a real, if not-yet-triggered, correctness bug.

Added a `FILE_SLOT_BYTES` constant and an early bounds check returning a distinct error code (`-3`) when a write is too large, replacing a magic-number `64` with the same named constant used in the check, so the two can't silently drift apart later.

Verified with a temporary, throwaway test (a 40KB static buffer written via `fs_write_file`, checked for the `-3` return, removed afterward) — confirmed via a visible "bounds check OK (rejected)" message on boot before removing the test code.

**A process lesson from this**, worth keeping: the temporary test file's cleanup step was missed initially, which left a `bigtest` entry permanently written into the actual `disk.img` — `fs.c` has no delete function, so there was no way to remove just that one entry. Had to wipe and let `fs_init()` reformat the disk from scratch. Real, concrete case for why file deletion is worth building eventually (see Next Steps) — not hypothetical, actually ran into it.

---

## 6. Multi-Line File Content

The other half of the filesystem work: `write` and `cat` previously only handled a single line of content each — an explicitly known limitation from earlier. Turned out to be two separate gaps, both in `shell.c` (the filesystem layer itself, `fs.c`, already stored raw bytes correctly, `\n` included):

- **`write` mode** only ever captured one line before saving and exiting.
- **`cat`** rendered a whole file as one `output_print()` call — since the font has no glyph for `\n`, newlines silently vanished, running every line together on screen even though the underlying bytes on disk were fine.

**Design decision:** multi-line `write` needed some way to know when input is finished, without breaking the ability to have a genuinely blank line as real content. Went with the classic Unix `mail`/`ed` convention — **a line containing only `.` ends input** — over "blank line ends it" (which would make blank lines inside a file impossible) or an invented keyword (less standard, harder to guess).

Implemented: a `writing_content` buffer that accumulates lines with `\n` separators as they're typed, only writing to disk once `.` is seen; `cat` now splits its buffer on `\n` and calls `output_print()` once per line.

Verified end-to-end with a real multi-line file: `write cleantest.txt`, three separate lines typed, `.` to finish, then `cat cleantest.txt` correctly printed all three lines back out separately, in order — confirmed both that the content survived the disk round-trip and that the newline-splitting fix actually works, not just the write side.

**One real, pre-existing limitation surfaced along the way, not fixed today:** the keyboard driver has no Shift-key handling at all, so characters like `_` genuinely cannot be typed right now (confirmed by checking `keyboard.c`'s scancode table directly, not guessed). Worth keeping in mind when naming files or typing content until Shift support exists.

---

## 7. Reflection

Every real bug found today — the heap exhaustion, the silent shell failure, the file-size overflow, even the confusion around multi-line input not "finishing" — was ultimately a case of trusting a claimed success (a printed message, or an assumption about what a bare Enter should do) over checking the actual, concrete state (`ps`'s real process list, the actual bytes in a file, the actual defined behavior of the `.`-terminator). Continuing to treat visible messages as claims rather than proof, and checking the underlying state directly, kept paying off today exactly like it did with Pillar 5's discoveries.

---

## 8. Where Things Stand

- Boot foundation, memory management — complete
- Processes, scheduling, syscalls, per-process page-table isolation, capability-based security — complete
- Disk driver — complete
- Filesystem — complete for current scope: file size bounds-checked, multi-line content fully supported and verified; no delete or free-space allocator yet (known, deliberate scope limit)
- Interactive shell — complete, now with real multi-line input
- Full ring 0 / ring 3 privilege separation (all 5 pillars) — complete, with direct register-level confirmation

---

## 9. Next Steps

- [ ] File deletion and a real free-space allocator for the filesystem (current version uses a fixed per-file-slot allocation with no way to reclaim space)
- [ ] Shift-key support in the keyboard driver (unlocks uppercase, symbols like `_`, and more natural text entry generally)
- [ ] Load real (non-hardcoded) user code into a ring-3 process — even a small custom binary format short of a full ELF loader
- [ ] Begin design discussion for a basic GUI layer built on top of the existing framebuffer and text rendering

---

*Log — Day 18, Infinity OS project.*
