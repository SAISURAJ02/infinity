# Infinity OS — Day 2 Progress Log

**Date:** August 18, 2026
**Project:** Infinity — hobby OS from scratch (x86-64)

---

## 1. Recap / Starting Point

Picked up from Day 1 with `kernel/src/kernel.c` and `kernel/src/limine.h` written but not yet compiled. Goal for today: get the kernel to actually compile and link, and move toward the first boot.

---

## 2. Linker Script

Created `linker.ld` at the **project root** (not inside `kernel/src` — it's build configuration, not source code, so it stays alongside the Makefile).

```ld
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(kernel_main)

PHDRS
{
    text    PT_LOAD    FLAGS((1 << 0) | (1 << 2)) ; /* Execute + Read */
    rodata  PT_LOAD    FLAGS((1 << 2)) ;             /* Read only */
    data    PT_LOAD    FLAGS((1 << 1) | (1 << 2)) ;  /* Write + Read */
}

SECTIONS
{
    . = 0xffffffff80000000;

    .text : {
        *(.text .text.*)
    } :text

    . += CONSTANT(MAXPAGESIZE);

    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    . += CONSTANT(MAXPAGESIZE);

    .data : {
        *(.data .data.*)
    } :data

    .bss : {
        *(COMMON)
        *(.bss .bss.*)
    } :data
}
```

**Purpose:** Tells the linker exactly where in memory the kernel should be placed (`0xffffffff80000000` — the conventional x86-64 "higher half" kernel address, matching the `-mcmodel=kernel` compile flag), and separates code/read-only-data/writable-data into distinct memory regions with correct permissions.

---

## 3. Makefile

Created `Makefile` at the project root to automate the full build pipeline (compile → link → ISO → run), instead of typing multi-step commands by hand every time.

```makefile
TARGET = x86_64-elf
CC = $(TARGET)-gcc
LD = $(TARGET)-gcc

CFLAGS = -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic \
         -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 \
         -mno-red-zone -mcmodel=kernel -Ikernel/src -Wall -Wextra

LDFLAGS = -T linker.ld -ffreestanding -nostdlib -static -m64 -mcmodel=kernel

SRC = kernel/src/kernel.c
OBJ = kernel/src/kernel.o

KERNEL = kernel/kernel.elf
ISO = infinity.iso

.PHONY: all clean run iso

all: $(KERNEL)

kernel/src/kernel.o: kernel/src/kernel.c
	$(CC) -c $< -o $@ $(CFLAGS)

$(KERNEL): $(OBJ)
	$(LD) $(LDFLAGS) -o $(KERNEL) $(OBJ)

iso: $(KERNEL)
	mkdir -p iso_root/boot/limine
	cp $(KERNEL) iso_root/boot/kernel.elf
	cp limine.conf iso_root/boot/limine/
	cp limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin iso_root/boot/limine/
	mkdir -p iso_root/EFI/BOOT
	cp limine/BOOTX64.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(ISO)
	./limine/limine bios-install $(ISO)

run: iso
	qemu-system-x86_64 -cdrom $(ISO) -m 512M

clean:
	rm -rf kernel/src/*.o kernel/kernel.elf $(ISO) iso_root
```

**Targets:**
- `make` → compile + link only
- `make iso` → also builds bootable ISO
- `make run` → build everything and boot in QEMU
- `make clean` → remove build artifacts

---

## 4. Problem: Cross-Compiler Not Found (`PATH` not persisted)

**Symptom:**
```
make: x86_64-elf-gcc: No such file or directory
make: *** [Makefile:24: kernel/src/kernel.o] Error 127
```

**Investigation:**
- Confirmed the actual compiler binaries were still safely present on disk at `~/osdev-toolchain/opt/cross/bin/` — nothing was lost.
- Checked `~/.bashrc` for the `PREFIX`/`TARGET`/`PATH` export lines added in Day 1 — **they were missing.** The earlier `echo ... >> ~/.bashrc` commands had not actually persisted, likely due to a session/timing issue that day.

**Fix:**
```bash
echo 'export PREFIX="$HOME/osdev-toolchain/opt/cross"' >> ~/.bashrc
echo 'export TARGET=x86_64-elf' >> ~/.bashrc
echo 'export PATH="$PREFIX/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```
Verified with `grep -A1 PREFIX ~/.bashrc` and `which x86_64-elf-gcc` before retrying the build.

**Lesson learned:** always verify `.bashrc` changes actually persisted (via `grep` or reopening a terminal) rather than assuming the append succeeded.

---

## 5. First Successful Kernel Compile 🎉

```bash
make
```
Output:
```
x86_64-elf-gcc -c kernel/src/kernel.c -o kernel/src/kernel.o -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -Ikernel/src -Wall -Wextra
x86_64-elf-gcc -T linker.ld -ffreestanding -nostdlib -static -m64 -mcmodel=kernel -o kernel/kernel.elf kernel/src/kernel.o
```
No errors, no warnings.

**Verified output binary:**
```bash
ls -la kernel/kernel.elf
# -rwxrwxr-x 1 suraj suraj 5160 Aug 17 23:46 kernel/kernel.elf
```

A real 5160-byte freestanding x86-64 ELF binary — Infinity's first compiled kernel. This is the first genuine "the OS is real code, not just files" milestone.

---

## 6. Git Commit & Push Issue

**Committed the milestone:**
```bash
git add linker.ld Makefile kernel/src/kernel.c kernel/src/limine.h
git commit -m "First successful kernel compile: linker script, Makefile, kernel.c"
```

**Confusion:** the terminal output after the commit looked like a `git status` message rather than a typical commit confirmation, which raised concern the commit hadn't worked.

**Verification method used:**
```bash
git log --oneline -3
```
This clearly showed the commit (`e1d71f8 ... First successful kernel compile...`) sitting at HEAD — confirming it had, in fact, succeeded. `git status` showing "ahead of origin by 1 commit" is itself proof a local commit exists; a failed commit would leave nothing to be "ahead" of.

**Push hang issue:**
```bash
git push
```
Hung indefinitely with no output or error. Diagnosed step by step:
1. Ran `ping -c 3 github.com` → no packet loss, general internet connectivity confirmed fine.
2. Cancelled the stuck push (`Ctrl+C`).
3. Closed and reopened the terminal entirely (fresh shell session, ruling out any stuck/broken state in the old one).
4. Re-ran `git push` in the new terminal — went through cleanly:
```
Enumerating objects: 9, done.
...
   9ddbb55..e1d71f8  main -> main
```

**Likely cause:** stale connection or an authentication token needing refresh in the old terminal session — resolved simply by starting a fresh shell.

---

## 7. In Progress: Cleaning Up `limine/` from Git Tracking

Identified that the cloned `limine/` folder (external bootloader binaries) was showing as untracked in `git status` and should **not** be committed to the repo — it's a third-party dependency, not Infinity's own source code, and contains large binary files unsuited for git history.

**Plan (to finish next):**
```bash
echo 'limine/' >> .gitignore
```
Then document the Limine setup steps in `README.md` so the dependency can be re-fetched by anyone (including future-self) without needing it committed:
```bash
git clone https://github.com/limine-bootloader/limine.git --branch=v7.x-binary --depth=1
cd limine && make && cd ..
```
Followed by:
```bash
git add .gitignore README.md
git commit -m "Gitignore limine binary dependency, document setup in README"
git push
```

---

## 8. Next Steps

- [ ] Finish gitignoring `limine/` and documenting its setup in README
- [ ] Write `limine.conf` (bootloader menu configuration)
- [ ] Run `make iso` to build the bootable ISO
- [ ] Run `make run` to boot Infinity in QEMU for the first time
- [ ] Confirm the test pixel renders (proof the kernel + bootloader handoff works)
- [ ] Commit the first successful boot milestone

---

