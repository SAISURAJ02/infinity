# Infinity OS — Day 14

**Date:** September 5, 2026
**Focus:** Disk abstraction layer and ATA driver — design, implementation, and an ongoing debugging session

---

## 1. Recap / Starting Point

With processes, preemptive scheduling, syscalls, per-process isolation, and capability-based security all working, moved into the next area of work: persistent storage. A filesystem needs actual disk read/write capability, so today's focus was building that foundation.

---

## 2. Design Discussion: Interface Choice and Abstraction

Before writing any code, discussed which disk interface to target. Raised and worked through directly: why ATA/IDE (an older, simpler standard) instead of modern SATA AHCI or NVMe.

**Key distinctions clarified:**
- ATA/IDE: simple, synchronous, port-based programming (write a command to a fixed I/O port, poll a status register, read/write data through another port) — no PCI enumeration needed, works at fixed well-known ports, exactly matches the `inb`/`outb` pattern already used throughout the project (PIC, PIT, keyboard).
- SATA AHCI / NVMe: modern, queue-based, memory-mapped command structures, require PCI/PCIe bus enumeration (a subsystem not yet built) and significantly more complex command protocols.

**Addressed a real, legitimate concern directly:** whether choosing ATA now would create problems if the project is later extended toward more realistic/modern hardware support. Resolved by committing to a proper **abstraction layer**: a generic `disk_read_sector()`/`disk_write_sector()` interface (`disk.h`/`disk.c`) that all higher-level code (filesystem, shell) will exclusively use, with ATA as the first concrete backend underneath it (`ata.h`/`ata.c`). This mirrors the same pattern already used elsewhere in the project (`paging_map()` hiding page-table mechanics, `kmalloc`/`kfree` hiding block-splitting) — the goal being that adding a future AHCI or NVMe backend later becomes an additive, isolated change, not a rewrite of everything built on top.

---

## 3. Implementation

- **`disk.h`/`disk.c`** — the generic interface (`disk_read_sector(lba, buffer)`, `disk_write_sector(lba, buffer)`), forwarding directly to the ATA implementation for now.
- **`ata.h`/`ata.c`** — the actual ATA PIO-mode driver: selecting the drive, writing the LBA sector address across three ports, issuing the read/write command, polling the status register, and transferring 256 sixteen-bit words (512 bytes) per sector via the data port.
- **Added `inw`/`outw`** to `io.h` — word-sized port I/O, needed since ATA's data port transfers 16 bits at a time (previously only had byte-sized `inb`/`outb`).
- **Hit and fixed two small, now-familiar header self-containment issues:** `ata.h` used `uint32_t` without including `<stdint.h>` — the same class of bug seen several times before in the project, always resolved the same way.

---

## 4. First Test and a Missing Disk

Wrote `test_disk()`: write a known byte pattern to sector 100, read it back into a separate buffer, compare, and show a color-coded result. First run showed no result at all — only the earlier keyboard-flash indicator appeared, meaning execution never reached the test.

**Root cause:** QEMU was never given an actual hard disk — only the boot ISO via `-cdrom`. Created a blank raw disk image (`qemu-img create -f raw disk.img 64M`) and attempted to attach it via `-drive file=disk.img,format=raw,if=ata`, which QEMU rejected (`unsupported bus type 'ata'`) — corrected to the proper QEMU bus-type name, `if=ide` (a naming inconsistency in QEMU's own option syntax; ATA and IDE refer to the same hardware here). Added `disk.img` to `.gitignore` as a generated artifact, not source.

---

## 5. Debugging: Infinite Poll Loop

With a real disk attached, still no test result appeared. Investigated with the project's established debug-log methodology (`-d int,cpu_reset -D qemu_debug.log --no-reboot --no-shutdown`).

**Finding:** RIP frozen at an identical instruction across consecutive timer ticks — a genuine infinite loop, with the timer itself still healthy (confirming the freeze was isolated to the new disk code, not a broader regression). Decoded the actual status register value from the logged `RAX`: low byte `0x03`, meaning the ERR bit was set — but `ata_wait_ready()` only checked for BSY clearing and DRQ setting, never checking for ERR, so an error condition caused it to spin forever waiting for a "ready" state that would never arrive.

**First fix:** rewrote `ata_wait_ready()` to explicitly check the ERR bit and return an error code (`-1`) instead of looping indefinitely; updated both `ata_read_sector` and `ata_write_sector` to check this return value and propagate failure rather than assuming success. Also updated `test_disk()` to distinguish write-error, read-error, and data-mismatch as separate, distinctly colored outcomes, rather than a single pass/fail signal.

**Result: still hung**, but with the freeze point shifted (confirming the rebuild picked up the fix) and a different status value this time — low byte `0x50`, decoded as BSY clear, DRQ clear, ERR clear — a normal idle/ready status, not an error. This means the loop now correctly avoids misreading an error as success, but reveals a different underlying issue: the drive appears to never actually receive or act on the read/write command, since it never transitions to the DRQ (data request) state at all.

---

## 6. In Progress: Bounded Polling and On-Screen Status Diagnostics

Rather than continue inferring from register dumps alone, made two changes to get direct, on-screen ground truth:
- Made `ata_wait_ready()` **bounded** (100,000 attempts, returning a distinct timeout code `-2`) so a genuinely stuck drive can no longer hang the kernel indefinitely, regardless of root cause.
- Exposed the last-read status byte (`g_last_ata_status`) so it can be displayed directly using the existing text-rendering system, rather than only being visible through the debug log.

**Not yet complete as of this log:** updating `test_disk()` to actually print this status value as hex text on screen, and identifying why the ATA command sequence isn't producing a DRQ response from the emulated drive.

---

## 7. Reflection

This is a good example of the project's established debugging discipline continuing to hold up on new, unfamiliar hardware territory: rather than assuming the first plausible fix (checking for ERR) was sufficient, the same log-based verification step was applied afterward and correctly caught that the real behavior had changed but the underlying problem had not fully resolved. Moving to bounded polling plus direct on-screen diagnostics is a deliberate step to stop relying solely on register-dump interpretation, which has been informative but slow to iterate on.

---

## 8. Next Steps

- [ ] Display `g_last_ata_status` as hex text on screen using the existing font/text renderer, for direct, fast-to-read diagnostics
- [ ] Investigate why the ATA command sequence isn't producing a DRQ transition — candidates include incorrect drive/head selection bits, command timing, or an emulation-specific quirk in how the attached disk image is recognized
- [ ] Once basic sector read/write is confirmed working: design a minimal on-disk filesystem format
- [ ] Continue toward a real interactive shell once the filesystem layer is functional

---

*Log — Day 14, Infinity OS project.*
