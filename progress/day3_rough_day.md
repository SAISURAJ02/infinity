# Infinity OS — Day 3 (Rough Day — Off-Project)

**Date:** August 19, 2026
**Status:** No Infinity work today — full day spent troubleshooting and fixing a broken dual-boot setup on the primary dev machine.

> Labeled as a "Rough Day" rather than skipped entirely, since real systems troubleshooting happened and the underlying skills (UEFI, NVRAM, partition tables, bootloaders) are directly relevant to the project.

---

## 1. What Happened

Woke up to the laptop (Lenovo LOQ 15IAX9 — the same machine Infinity is developed on) failing to boot into either Windows or Ubuntu, showing:
```
>>Start PXE over IPv4.
PXE-E16: No valid offer received.
...
EFI PXE 0 for IPv4 (38-A7-46-37-31-04) boot failed.
```

**Confirmed immediately: this was unrelated to Infinity.** All Infinity development and testing has only ever happened inside QEMU — nothing had touched the real bootloader or disk.

---

## 2. Root Cause #1 — Missing UEFI NVRAM Boot Entries

Diagnosed via BIOS (F2 setup → Boot tab): the UEFI boot entry list contained only **one entry — "EFI PXE Network"**. Both the Windows Boot Manager and Ubuntu/GRUB entries had vanished.

**Key concept learned:** UEFI firmware keeps a small list in a separate non-volatile chip on the motherboard (**NVRAM**) — distinct from the SSD — that stores "pointer" entries: *"the bootloader lives at this exact path, on this exact partition."* These pointers had been wiped (likely a BIOS reset/update/power event), while the actual OS installations and files remained fully intact — confirmed by checking BIOS's Information tab, which still showed the NVMe SSD (`Micron MTFDKCD512QGN-1BN1AABLA`) detected and healthy.

---

## 3. Fix Attempt 1 — Boot-Repair via Ubuntu Live USB

1. Created a bootable Ubuntu USB (64-bit AMD64 ISO, via Rufus, GPT/UEFI target) on a separate laptop (ASUS ROG, Ryzen 7).
2. First boot attempt into the live USB was accidentally in **Legacy/CSM mode**, not UEFI — Boot-Repair wrongly suggested creating a BIOS-Boot partition. Recognized this as the wrong fix for a UEFI/GPT system and did not proceed.
3. Rebooted, correctly selected the **UEFI-mode** boot entry this time (confirmed via a proper UEFI GRUB menu showing "UEFI Firmware Settings").
4. Ran Boot-Repair → **Recommended repair**.

**Result:** Ubuntu's boot entry was successfully restored — confirmed by booting into the actual installed Ubuntu (not the live USB).

**Windows Boot Manager was NOT restored.** Confirmed via `efibootmgr -v` from Ubuntu — only PXE, ubuntu, and generic USB/network entries existed. No Windows entry.

---

## 4. Root Cause #2 — Ubuntu Emergency Mode (new problem introduced by the fix)

After the Boot-Repair fix, Ubuntu started dropping into a `systemd` emergency shell on boot instead of loading normally.

**Diagnosis:** Boot-Repair had changed the EFI System Partition's UUID while fixing the boot entry, but `/etc/fstab` still referenced the **old** UUID for `/boot/efi` — causing the mount to fail at boot and triggering emergency mode.

**Fix:**
```bash
sed -i 's/CEAD-4649/76FB-D5AF/' /etc/fstab
```
Confirmed Ubuntu booted cleanly afterward.

---

## 5. Root Cause #3 — Windows Boot Files Missing Entirely (not just an NVRAM entry)

Mounted the EFI System Partition from Ubuntu to inspect it:
```bash
sudo mkdir -p /mnt/efi
sudo mount /dev/nvme0n1p1 /mnt/efi
ls /mnt/efi/EFI/    # → only BOOT and ubuntu folders — no "Microsoft" folder at all
```

This meant Windows's actual boot files (`bootmgfw.efi`, etc.) were missing from the partition itself — a deeper problem than Ubuntu's had been (which was just a missing pointer).

**Confirmed Windows itself was fully intact**, by mounting and inspecting:
- WinRE partition (`/dev/nvme0n1p6`, 2GB) — Recovery folder present
- Main Windows partition (`/dev/nvme0n1p3`, 367GB NTFS) — `Windows`, `Users`, `Program Files` all present and untouched

**Decision:** rebuild the missing boot files using official Windows Recovery Environment tools (`bootrec`, `bcdboot`) rather than hand-crafting EFI files from Linux.

---

## 6. Fix Attempt 2 — Windows Recovery via Windows RE

1. **First USB attempt** (Microsoft Media Creation Tool) failed — stuck at 0% download, and once made, didn't boot as UEFI (booted straight to Ubuntu instead of Windows Setup). Abandoned.
2. **Second USB attempt** — used Rufus with a Windows 11 ISO already on hand, GPT partition scheme + UEFI target (Rufus auto-configured this correctly). Booted successfully via F12 → "EFI USB Device" → reached actual Windows Setup screen.
3. Clicking **"Repair your computer"** unexpectedly dropped into Ubuntu's emergency mode instead of Windows RE the first time — a secondary symptom of the same stale-fstab issue (Root Cause #2), fixed in parallel via the `sed` command above.
4. Successfully entered **Windows RE → Troubleshoot → Advanced options → Command Prompt**.

### Diskpart investigation:
```
list volume     → EFI System Partition doesn't show (expected, no drive letter yet)
select disk 0
list partition  → Partition 1 (260MB) = ESP, but showing type "Unknown" instead of "System"
```

**Root Cause #4 discovered:** Boot-Repair had also corrupted the ESP's **partition type GUID** — the same underlying corruption event that broke the fstab UUID. A healthy ESP should show as type "System" in diskpart; this one didn't, so diskpart wouldn't create a volume object for it or allow a drive letter.

**Fix:**
```
select partition 1
set id=c12a7328-f81f-11d2-ba4b-00a0c93ec93b
list partition   # confirmed: now shows type "System"
assign letter=S
exit
dir S:           # confirmed: EFI folder present, no Microsoft subfolder (as expected)
```

### Rebuilding the boot files:
```
bootrec /fixboot
```
→ "Access is denied" — identified as a known, harmless quirk on UEFI/GPT systems (fixboot writes legacy BIOS-style boot sector code that doesn't apply the same way to EFI boot); safely skipped.

```
bcdboot C:\Windows /l en-us /s S: /f UEFI
```
→ **"Boot files successfully created."** ✅

---

## 7. Confirming Both OSes Boot

Restarted, removed the USB, checked F12 boot menu:
- **Windows** now booted — automatically, with no menu, since `bcdboot` had registered it as the new default UEFI boot entry.
- **Ubuntu** confirmed still bootable via F12 → selecting the ubuntu entry.

Both operating systems working independently at this point, but **no menu was shown by default** — required pressing F12 every time to choose.

---

## 8. Polish — Restoring the Boot Menu Experience

**Problem 1: Windows was skipping straight in (no menu at all).**
Diagnosed via `sudo efibootmgr -v` — boot order was `0004 (Windows), 0003 (ubuntu), ...`, so Windows loaded first with no chance to choose.

**Fix:**
```bash
sudo efibootmgr -o 0003,0004,2001,2002,2003
```
Reordered so Ubuntu's GRUB loads first, since GRUB itself can present a menu for both OSes.

**Problem 2: Ubuntu now loaded directly, still no menu.**
Checked `/etc/default/grub` — found:
```
GRUB_TIMEOUT_STYLE=hidden
GRUB_TIMEOUT=0
```
**Fix:** changed to:
```
GRUB_TIMEOUT_STYLE=menu
GRUB_TIMEOUT=5
```
then `sudo update-grub`.

**Problem 3: GRUB menu appeared, but Windows wasn't listed at all.**
Modern Ubuntu disables `os-prober` (the tool that detects other installed OSes) by default, for security reasons.

**Fix:**
```bash
sudo nano /etc/default/grub
# Uncommented: GRUB_DISABLE_OS_PROBER=false
sudo apt install os-prober
sudo update-grub
```
Output confirmed: `Found Windows Boot Manager on /dev/nvme0n1p1`.

---

## 9. Final Result ✅

Rebooting now shows a proper **GRUB menu on every boot**, listing both **Ubuntu** and **Windows Boot Manager**, selectable with arrow keys — no F12 required. Full dual-boot functionality restored, exactly as it was before the original failure.

---

## 10. Root Causes — Full Chain

A single event (likely a BIOS reset/update) triggered a chain of **five distinct, compounding problems**, each diagnosed and fixed in sequence:

1. Missing UEFI NVRAM boot entries (both OSes)
2. Stale ESP UUID in Ubuntu's `/etc/fstab` (introduced by the first fix) → emergency mode
3. Windows boot files themselves missing from the ESP (not just a pointer)
4. Corrupted ESP partition type GUID (showing "Unknown" instead of "System")
5. Boot order + GRUB config (timeout hidden, os-prober disabled) hiding the menu after everything else was technically fixed

---

## 11. Key Concepts Reinforced Today

- **NVRAM vs disk storage** — small separate firmware memory chip holding boot pointers, distinct from the SSD holding actual OS files
- **EFI System Partition (ESP)** — a small FAT32 partition holding bootloader files (`\EFI\Microsoft\...`, `\EFI\ubuntu\...`), and the importance of its **partition type GUID** (`c12a7328-...`) being correctly set for tools like diskpart to recognize it
- **`bcdboot`** — the correct, official tool for rebuilding Windows's boot files onto an ESP, given an intact `C:\Windows` installation
- **GRUB as a menu, not just a bootloader** — GRUB itself can chain-load other OSes (like Windows Boot Manager) if `os-prober` detects them and it's configured to show a menu
- **UEFI boot order (`efibootmgr -o`)** — controls which entry the firmware tries first, independent of what each individual bootloader does once it's running

---

## 12. Reflection

This was a genuinely deep, real-world systems debugging exercise — five compounding issues across BIOS/UEFI, Linux (`fstab`, GRUB, `efibootmgr`), and Windows (`diskpart`, `bcdboot`) — all resolved without any data loss and without reinstalling either OS. While not Infinity codebase work, this is directly relevant experience for an OS-focused project and portfolio.

**No Infinity code was touched today.** Resuming tomorrow from the IDT (Interrupt Descriptor Table) implementation, right where Day 2 left off.

---

*Log — Day 3, Infinity OS project (off-project troubleshooting day).*
