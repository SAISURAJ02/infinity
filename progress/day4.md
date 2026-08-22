# Infinity OS — Day 5

**Date:** August 22, 2026
**Focus:** Finishing PIC/hardware interrupts, keyboard driver

---

## 1. Recap / Starting Point

Picked up from the previous session with `io.h` and `pic.h`/`pic.c` written (port I/O helpers and PIC remapping logic) but not yet wired into the build or tested. Goal for today: finish hardware interrupt handling end-to-end.

---

## 2. Wiring PIC + Timer IRQ (IRQ0)

- Added `pic.o` to the Makefile's `OBJS`.
- Wrote `irq.asm`: `irq0` stub, pushing a dummy error code + interrupt number 32, jumping into a shared `irq_common_stub` (same save/call/restore/`iretq` pattern as the exception ISRs, but simpler — hardware interrupts never have a CPU-provided error code).
- Added `irq_handler` in `idt.c`, wired IDT entry 32 → `irq0`.
- Called `pic_remap()` and executed `sti` (set interrupt flag) in `kernel_main`, right after `idt_init()`.

**Hit one syntax bug along the way:** `pic.h` used `uint8_t` without including `<stdint.h>` — worked in other headers only because of include order luck in `kernel.c`; fixed by adding the include directly to `pic.h` so it's self-contained.

**First real test:** rebuilt and ran — red square stayed stable with the timer now firing continuously and silently in the background (dozens of times/second), each interrupt going through the full save/handle/EOI/`iretq` cycle with zero visible disruption. Confirmed working.

---

## 3. Concept: I/O Ports (recap, already covered but reinforced today)

Solidified understanding that x86 I/O ports (`in`/`out` instructions) are a completely separate 16-bit address space from RAM — not the same as physical USB-style ports — used for legacy hardware like the PIC for historical/backward-compatibility reasons. PIC uses ports `0x20`/`0x21` (master) and `0xA0`/`0xA1` (slave).

---

## 4. Adding Keyboard IRQ (IRQ1)

- Added `irq1` stub to `irq.asm` (same pattern as `irq0`, pushing interrupt number 33 instead of 32).
- Updated `irq_handler` in `idt.c` to branch: if `int_no == 33`, read the scan code via `inb(0x60)`, check the top bit to distinguish key press vs release (Scan Code Set 1: press = raw code, release = same code with bit 0x80 set).
- Wired IDT entry 33 → `irq1`.
- First test version: flash the whole screen green and halt on any keypress, to prove the pipeline end-to-end (same "obvious visual proof" strategy used for the earlier divide-by-zero test).

---

## 5. Debugging: Keyboard Interrupt Not Firing

**Symptom:** red square stayed stable, but pressing keys inside QEMU produced no reaction at all — no green screen, no crash, nothing.

**Ruled out systematically:**
1. Confirmed QEMU window focus was genuinely transferring correctly (title bar showed "Press Ctrl+Alt+G to release grab" — confirms keyboard input was going to the VM).
2. Confirmed the simplified "flash on ANY keyboard event" test version was actually saved and rebuilt (initially it wasn't — a `grep` check caught this before wasting more debugging time on the wrong code).
3. With the simplified version genuinely running, keypresses still produced no reaction — ruled out a bug in the scan-code bit-check logic specifically, and pointed to something earlier in the pipeline.

**Root cause found:** `pic_remap()` changes *where* interrupts are routed in the IDT, but does **not** guarantee they're *enabled*. IRQ1 (keyboard) was masked by default in the PIC's interrupt mask register — a completely separate, independent control from the remapping itself.

**Fix:**
```c
// Unmask IRQ0 (timer) and IRQ1 (keyboard) — pic_remap() alone doesn't guarantee these are enabled
outb(0x21, inb(0x21) & ~0b00000011);
```
Added right after `pic_remap()`, before `sti`. This explicitly clears the mask bits for IRQ0 and IRQ1 on the master PIC's data port (`0x21`), regardless of whatever the original default mask was.

**Result:** immediate fix — keyboard events started reaching the handler correctly.

**Key lesson (genuinely valuable, worth remembering):** *remapping* and *masking* are two separate, independent layers of PIC control. Getting the IDT entries and remap sequence perfectly correct means nothing if the specific IRQ line is still masked — this is an easy, non-obvious trap.

---

## 6. Cleaning Up Into a Real (Non-Halting) Keyboard Driver

The test version halted the whole kernel on any keypress — fine for proving the pipeline works, but not real driver behavior. Replaced with `keyboard_flash()` in `kernel.c`: a small 20×20 pixel indicator in the top-left corner that toggles between green and blue on every key press, without halting — the kernel keeps running normally, ready to respond to the next keystroke immediately.

Restored proper press-only filtering (ignoring key releases) in `irq_handler`.

**Final test:** pressed multiple different keys in sequence — indicator toggled color correctly on every single press, kernel never froze. Confirmed working.

---

## 7. Complete ✅

- ✅ Toolchain, Limine boot, 64-bit long mode handoff
- ✅ Kernel-owned GDT (ring 0 segments)
- ✅ Kernel-owned IDT (CPU exceptions, visually verified via divide-by-zero test)
- ✅ PIC remapping + hardware interrupts (timer AND keyboard, both tested live, non-halting)

Committed and pushed:
```
git commit -m "Add keyboard IRQ handling: non-halting keypress indicator, fixed PIC IRQ1 masking bug. Month 1 complete."
```

---

## 8.Memory Management

Started the conceptual introduction to Month 2. Framing established:
- Currently, Infinity has **zero memory management** — the kernel writes directly to arbitrary physical addresses (e.g., the framebuffer) with no tracking, no protection, no organization.
- This must be fixed before Month 3 (processes), since process isolation fundamentally depends on proper memory management.
- **Three layers planned, in order:**
  1. **Physical Memory Manager (PMM)** — track which physical RAM frames are free vs. in use
  2. **Paging / Virtual Memory** — CPU-level translation from virtual to physical addresses, with protection
  3. **Kernel heap allocator** — a real `malloc`/`free` built on top of the above

**First concept introduced:** RAM is divided into fixed-size **frames** (conventionally 4KB each — so 512MB of QEMU RAM = ~131,000 frames to track). Noted that Limine can supply a memory map at boot (similar to how the framebuffer was requested), telling us which regions are usable vs. reserved.

**Currently mid-discussion:** thinking through how to efficiently track free/used state for ~131,000 frames (bitmap vs. other approaches) before writing any PMM code — continuing this in the next session.

---

## 9. Next Steps

- [ ] Finish the PMM data-structure discussion (bitmap approach) and understand *why* it's the standard choice
- [ ] Request Limine's memory map at boot
- [ ] Implement the physical memory manager (bitmap-based frame allocator)
- [ ] Move on to paging (virtual memory) once PMM is solid
- [ ] Eventually: kernel heap allocator (`malloc`/`free`)

---

*Log — Day 5, Infinity OS project.*
