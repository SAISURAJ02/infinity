# Infinity OS — Day 13

**Date:** September 3, 2026
**Focus:** Capability-based security implementation and demo, next-phase planning

---

## 1. Recap / Starting Point

With per-process address space isolation verified working (CR3 genuinely alternating between distinct physical addresses on consecutive timer ticks), the mechanical foundation for capability-based security was finally in place. Today's work: design and implement the actual signature feature.

---

## 2. Scoping the First Capability

Discussed which resource to gate for the first demonstrable capability check. Decided against reusing the existing `SYS_TEST` syscall (its action — flashing an indicator box — doesn't read as a meaningful "protected resource"), in favor of adding a new, purpose-built syscall: **`SYS_WRITE_PIXEL`**, gated by a capability that scopes *where on screen* a process is allowed to draw. This reads as a genuinely reasonable thing to protect (imagine multiple processes sharing a display) and maps cleanly onto real capability system principles: narrow, resource-specific grants rather than broad on/off permissions.

---

## 3. Data Structures

Added to `process.h`:
```c
#define MAX_CAPABILITIES 8

typedef enum {
    CAP_NONE = 0,
    CAP_DRAW_REGION,   // permission to draw pixels within a specific screen region
} capability_type_t;

struct capability {
    capability_type_t type;
    uint32_t x_min, x_max, y_min, y_max; // for CAP_DRAW_REGION: the allowed region
};
```
Extended `struct process` with a fixed-size capability table (`struct capability capabilities[MAX_CAPABILITIES]`) and a count. Caught and fixed one real bug during implementation: `process_create()` never initialized `capability_count` to `0`, meaning it would have started as uninitialized garbage from `kmalloc` (which doesn't zero memory) rather than a clean empty table — fixed by explicitly setting it in `process_create()`.

---

## 4. Core Functions

- **`process_grant_capability()`** — appends a new capability (type + bounding region) to a process's table, up to `MAX_CAPABILITIES`.
- **`process_check_capability()`** — given a process, a capability type, and an (x, y) coordinate, walks the process's capability table checking both **type match** and **bounds match** (`x_min <= x <= x_max` and `y_min <= y <= y_max`); returns true on the first matching capability found, false if none match.
- **`process_get_current()`** — exposes the currently-scheduled process, needed so the syscall handler can identify *which* process is making a given request.

---

## 5. Enforcement at the Syscall Boundary

Implemented `SYS_WRITE_PIXEL` (syscall number 1) in `syscall.c`. Calling convention: x and y packed into a single register (`rbx`, x in low 16 bits, y in next 16 bits), color passed via `rcx`. The handler:
1. Unpacks the coordinates and color
2. Retrieves the calling process via `process_get_current()`
3. Calls `process_check_capability(caller, CAP_DRAW_REGION, x, y)`
4. Only writes the pixel if the check passes; otherwise returns `-1` (access denied) without touching the framebuffer

This makes the syscall dispatcher the genuine enforcement point — exactly the architectural role identified back when the syscall mechanism was first built.

**Hit one small compile error along the way:** `syscall.c` used `NULL` without including `<stddef.h>` (where it's defined) — quick, obvious fix.

---

## 6. The Demo

Rewrote both test processes to route their drawing through the new syscall instead of writing directly to the framebuffer:
- **`test_process_1`** — granted `process_grant_capability(p1, CAP_DRAW_REGION, 50, 70, 300, 320)`, exactly matching its own drawing region. Every `do_syscall_write_pixel()` call passes the check.
- **`test_process_2`** — created with **zero capabilities**, deliberately never granted anything. Every one of its syscall write attempts (hundreds per frame) gets checked and denied.

**Result, confirmed visually:** the blue box (`test_process_1`) renders normally. The magenta box (`test_process_2`) **never appears at all** — not a single pixel gets through, despite the process actively attempting to draw every frame. This is a genuine, working security boundary, not a cosmetic difference — the kernel is actively and correctly rejecting unauthorized access at the syscall level.

Committed:
```
git commit -m "Implement capability-based security (signature feature): per-process capability table with CAP_DRAW_REGION type, new SYS_WRITE_PIXEL syscall enforcing capability checks via process_check_capability()..."
```

---

## 7. Milestone: Signature Feature Complete

This closes out the core promise made on Day 1 — Infinity now has a genuinely distinctive architectural feature, not just "another hobby OS that boots." The full story, provable end-to-end:
- Processes are real and preemptively scheduled (verified via interrupt counts)
- Processes are isolated at the hardware level (verified via alternating CR3 physical addresses)
- Access to resources is governed by explicit, narrow, unforgeable capabilities — not identity-based inheritance — enforced at the syscall boundary
- The denial is real and demonstrated, not asserted

---

## 8. Where the Project Stands

- **Boot foundation** — complete (toolchain, Limine, GDT, IDT, PIC/interrupts)
- **Memory management** — complete (physical memory manager, paging, kernel heap)
- **Processes and security** — core complete (process creation, verified preemptive scheduling, syscalls, verified per-process isolation, working capability-based security)
- **Filesystem, shell, GUI** — not yet started

---

## 9. Planning the Next Phase

With the core OS and signature feature genuinely working, discussed the next phase's scope honestly and concretely:

1. **A basic filesystem** — simple on-disk structure (superblock, flat/lightly-nested directories, fixed-size blocks), requiring a disk driver (ATA/IDE, simplest option under QEMU) and basic operations (create, read, write, delete, list).
2. **A real interactive text-mode shell** — extending the keyboard driver from "flash on keypress" to actual scan-code-to-ASCII character input, with a command line supporting `ls`, `cat`, running programs, `ps` (surfacing the real scheduler), and specifically **a command that interactively demonstrates the capability system** — attempting an operation without the right capability and visibly showing denial, making the signature feature demoable live rather than only via two hardcoded test processes.
3. **A basic GUI layer, built on top of the shell** (not replacing it) — simple windowed regions on the framebuffer, a couple of simple built-in applications, leveraging the existing framebuffer/text-rendering foundation.
4. **Explicitly out of scope, documented as future roadmap:** a real web browser and general third-party application installation — reconfirmed from an earlier scope conversation as requiring infrastructure (a full libc, dynamic linking, a TCP/IP + TLS stack) far beyond what's realistic right now, to be captured honestly in the README as long-term ambitions rather than near-term deliverables.
5. **Possibly:** triple-boot on real hardware once stable enough to trust outside QEMU.

**Not yet decided:** exact pacing for this next phase — to be resolved before starting implementation work.

---

## 10. Next Steps

- [ ] Decide scope/pacing for the next phase of work
- [ ] If proceeding: start with a disk driver (ATA/IDE) and basic filesystem, since the shell depends on it
- [ ] Write the "future roadmap" section of the README (browser, app installation, triple-boot) so the project's honest long-term vision is documented
- [ ] Consider a documentation/polish pass on the existing core (architecture overview, security model explanation)

---

*Log — Day 13, Infinity OS project.*
