# Infinity OS — Day 1 ( 16th August 2026 )Progress Log

**Date:** August 17, 2026
**Project:** Infinity — a hobby operating system built from scratch (x86-64)
**Goal:** Placement resume project + deep OS learning, 3–4 month timeline, 10+ hrs/week

---

## 1. Project Planning

- Decided to build an OS from scratch instead of a typical web/app project, to stand out for placements.
- Agreed on a 4-month roadmap:
  - **Month 1:** Boot into own kernel (bootloader, GDT/IDT)
  - **Month 2:** Memory management (paging, kernel heap, keyboard driver)
  - **Month 3:** Processes and multitasking (scheduler, syscalls)
  - **Month 4:** Filesystem, shell, polish, documentation
- Key resources identified: OSDev.org wiki, "Operating Systems: Three Easy Pieces" (OSTEP), "The Little Book About OS Development."
- Fallback plan if behind schedule: pivot to extending xv6 (MIT's teaching OS) instead of continuing fully from scratch.
- Named the project **Infinity**.

---

## 2. Architecture Decision: 32-bit vs 64-bit

- Originally planned 32-bit x86 (`i686-elf`) for simplicity.
- **Changed decision:** switched to **x86-64**, since the goal is to learn OS concepts *and* build something more current/impressive, despite the extra complexity of the real mode → protected mode → long mode transition.
- Noted trade-off: x86-64 adds ~1–2 extra weeks of "boot mechanics" work (temporary GDT, paging setup, enabling long mode via EFER) before reaching feature work.

---

## 3. Git & GitHub Setup

**Problems faced & solutions:**
| Problem | Cause | Fix |
|---|---|---|
| `bash: SAISURAJ02: No such file or directory` | Copied `git remote add origin <username>` literally with angle brackets — bash interpreted `<` as input redirection | Typed the actual username directly, no brackets |
| `remote: Repository not found` | Copied a markdown-style link `[text](url)`; terminal used the visible (wrong) text, not the real URL | Re-added remote with the plain, correct URL |
| `! [rejected] main -> main (fetch first)` | Remote repo (created via GitHub website) had commits (e.g. LICENSE) that local repo didn't have — divergent/unrelated histories | Ran `git config pull.rebase false` then `git pull origin main --allow-unrelated-histories`, resolved merge, then pushed successfully |

**Final repo setup includes:**
- `.gitignore` (ignoring `*.o`, `*.bin`, `*.iso`, `*.elf`, `build/`, `isodir/`, `*.img`)
- `README.md` with project description and goals
- `LICENSE` — chose **MIT License** (permissive, standard for portfolio/learning projects, simplest for others to read/fork/learn from, contrasted against GPL's copyleft and Apache 2.0's added complexity)
- Repo: `https://github.com/SAISURAJ02/infinity`
- Established practice: commit after every real milestone with descriptive messages, to build a clean, trackable history (also useful as an interview talking point).

---

## 4. Cross-Compiler Toolchain (x86_64-elf-gcc)

**Why needed:** System `gcc` targets the host OS (Ubuntu) and assumes a standard library exists. Kernel code needs a *freestanding* compiler with no host-OS assumptions — hence building a dedicated cross-compiler targeting `x86_64-elf`.

**Environment variables used:**
```bash
export PREFIX="$HOME/osdev-toolchain/opt/cross"
export TARGET=x86_64-elf
export PATH="$PREFIX/bin:$PATH"
```
Made permanent by appending to `~/.bashrc`.

**Built in `~/Desktop/project/osdev-toolchain/`:**

1. **binutils 2.42** — assembler/linker for the target
   ```bash
   wget https://ftp.gnu.org/gnu/binutils/binutils-2.42.tar.gz
   tar -xzf binutils-2.42.tar.gz
   mkdir build-binutils && cd build-binutils
   ../binutils-2.42/configure --target=$TARGET --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror
   make -j$(nproc)
   make install
   ```

2. **gcc 13.2.0** — the cross-compiler itself
   ```bash
   wget https://ftp.gnu.org/gnu/gcc/gcc-13.2.0/gcc-13.2.0.tar.gz
   tar -xzf gcc-13.2.0.tar.gz
   cd gcc-13.2.0
   ./contrib/download_prerequisites
   cd ..
   mkdir build-gcc && cd build-gcc
   ../gcc-13.2.0/configure --target=$TARGET --prefix="$PREFIX" --disable-nls --enable-languages=c,c++ --without-headers
   make all-gcc -j$(nproc)
   make all-target-libgcc -j$(nproc)
   make install-gcc
   make install-target-libgcc
   ```

**Problem faced:** A `cd cd ~/Desktop/...` typo silently failed, causing the gcc download/extraction/prerequisites steps to run inside the wrong directory (`build-binutils` instead of `osdev-toolchain`). Fixed by moving the already-downloaded/prepped files to the correct location with `mv`, avoiding a wasted re-download.

**Verification:**
```bash
x86_64-elf-gcc --version
# → x86_64-elf-gcc (GCC) 13.2.0 ✅
```

**Toolchain paths (kept distinct intentionally):**
- Toolchain source/build files: `~/Desktop/project/osdev-toolchain/`
- Installed cross-compiler binaries: `~/osdev-toolchain/opt/cross/bin/`

Documented in the repo as `TOOLCHAIN.md` for reproducibility, committed and pushed.

---

## 5. Bootloader Decision: GRUB vs Limine

**Question raised:** Why not use GRUB, since it's the industry-standard bootloader used by real Linux distros?

**Key finding via research:**
- GRUB's Multiboot2 handoff **always** leaves the CPU in 32-bit protected mode, regardless of target hardware.
- On real 64-bit systems (e.g. Ubuntu on this laptop), it's actually the **Linux kernel's own boot stub** (baked into `vmlinuz`) that performs the 32-bit → 64-bit long mode transition *after* GRUB hands off — not GRUB itself.
- For a hobby OS, using GRUB would mean writing that entire long-mode transition stub manually, from scratch.
- **Limine**, by contrast, hands off control to the kernel *already in 64-bit long mode*, removing that boilerplate while preserving all the actual learning (paging, GDT, IDT still need to be built by us afterward).

**Real-world adoption context (researched):**
- GRUB 2 remains the most popular Linux bootloader overall, especially for dual-boot/multiboot setups.
- Limine has gained real production adoption in 2026 — default in distributions like CachyOS and Omarchy, added as an option in KaOS 2025.11.
- In the hobby-OS community, Limine is used by projects like Cosmos and supported by SerenityOS, and is the de facto standard for modern 64-bit hobby kernels.
- Both GRUB and Limine support handing the kernel a linear framebuffer for graphics — relevant since Infinity plans a GUI later. Limine's framebuffer handling is considered cleaner/more first-class for this purpose.

**Decision: Use Limine** for the x86-64 target.

**Setup performed:**
```bash
cd ~/Desktop/project/infinity
git clone https://github.com/limine-bootloader/limine.git --branch=v7.x-binary --depth=1
cd limine
make
cd ..
mkdir -p kernel/src
mkdir -p iso_root
```
Verified `limine/` contains bootloader binaries (BIOS/UEFI), `limine.h` (protocol header), and the `limine` deploy tool.

---

## 6. Future Plan: Triple-Boot

- Currently dual-booting Linux + Windows.
- Plan to add Infinity as a third bootable OS after Month 4, once stable, using chainloading (supported by GRUB, Limine, rEFInd) — the existing boot menu would get a third entry.
- Development/testing will continue exclusively in QEMU until Infinity is stable enough to risk real hardware.

---

## 7. "Signature Feature" Brainstorm

Discussed making Infinity distinctive beyond just "another hobby OS," given the reality that a solo 4-month project can't out-perform Linux/Windows on raw capability. Reframed goal: **finishing a working OS is itself rare and valuable**; one well-chosen, well-explained "signature feature" adds further distinction.

**Ideas discussed (with today/problem/solution framing):**

1. **Capability-based security** — Traditional OSes (Linux/Windows) tie permissions to user identity; any program you run inherits your full access, even if it only needs one file. This is a common real-world attack vector. Alternative: give each process unforgeable "capability" tokens for only the specific resources it needs (used in real systems like seL4, Fuchsia).

2. **Batched syscalls (io_uring-style)** — Traditional syscalls handle one request at a time, with real per-call overhead; Linux had to retrofit io_uring (2019) to fix this. Infinity could build a queue/batch-based syscall interface from day one instead of retrofitting later.

3. **Built-in kernel tracing/observability** — Debugging a crashing kernel today gives no information (just a QEMU reboot). Directly solves a pain point already being felt during development. Idea: an in-kernel ring buffer logging interrupts/context switches/syscalls, inspectable after a crash.

4. **Alternative CPU scheduler** — Linux's default (CFS) optimizes for mathematical fairness, not necessarily perceived responsiveness, which is why alternative schedulers (e.g. BFS/MuQSS) exist as patches. Idea: let processes declare latency-sensitivity hints instead of pure round-robin or complex fairness math.

**Decision: Going with Capability-Based Security (#1) as Infinity's signature feature.**
Reasoning:
- More distinctive than #2 — fewer hobby OS projects attempt it.
- Integrates naturally with Month 3 process-management work rather than needing a separate demo to prove value.
- Strong interview narrative, especially relevant given a background in cybersecurity — connects systems work directly to security expertise.
- Design implication for later: process/resource handles should be built as capabilities from the start during Month 3, not retrofitted afterward.

---

## 8. Kernel Code — Started

Began writing the first kernel source files (not yet completed/tested):

**`kernel/src/kernel.c`** (initial draft — requests a framebuffer from Limine, draws a single test pixel, halts):
```c
#include <stdint.h>
#include <stddef.h>
#include "limine.h"

static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

static void hcf(void) {
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void kernel_main(void) {
    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    uint32_t *fb_ptr = (uint32_t *)fb->address;
    fb_ptr[100 * (fb->pitch / 4) + 100] = 0xFFFFFFFF;

    hcf();
}
```

**`kernel/src/limine.h`** — copied from `limine/limine.h` (the shared protocol header defining boot request/response structures between bootloader and kernel).

**Learning point covered:** difference between `.c` files (implementation/logic) and `.h` files (declarations/"contracts" — struct definitions, function signatures) that let separate files agree on shared structures without duplicating code.

**Compile/link pipeline discussed** (not yet executed — Makefile to be written next session):
```bash
# Compile step
x86_64-elf-gcc -c kernel/src/kernel.c -o kernel/src/kernel.o \
  -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic \
  -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 \
  -mno-red-zone -mcmodel=kernel -I kernel/src

# Link step (requires a linker script, not yet written)
x86_64-elf-gcc -T linker.ld -o kernel/kernel.elf kernel/src/kernel.o \
  -ffreestanding -nostdlib -static -m64 -mcmodel=kernel
```
Key point: kernel builds require a **multi-step pipeline** (compile → link with a custom linker script → package into an ISO with Limine → boot in QEMU) instead of a single `gcc file.c -o output` command, because there's no host OS to link against and full control over memory layout is required. A Makefile will automate this sequence.

---

## 9. Next Steps (for tomorrow)

- [ ] Finish creating `kernel/src/kernel.c` and `kernel/src/limine.h` on disk
- [ ] Write the linker script (`linker.ld`) to control kernel memory layout
- [ ] Write a Makefile to automate compile → link → ISO build
- [ ] Assemble `iso_root/` with kernel binary + Limine files + `limine.conf`
- [ ] Build bootable ISO and boot it in QEMU for the first time
- [ ] Commit working "Hello World" kernel milestone to git

---

*Log generated at the end of Day 1 — Infinity OS project.*
