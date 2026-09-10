    Infinity OS is an experimental, 64-bit monolithic operating system built from scratch for the x86-64 architecture. It features a custom capability-based security model, 4-level
  paging with a Higher-Half Direct Map (HHDM), preemptive multitasking, an ATA PIO storage driver, a custom filesystem, and an interactive framebuffer text console.
    
    ---
    
    ## Architecture Overview
    
    * **Architecture:** x86-64 (AMD64 Long Mode)
    * **Boot Protocol:** Limine Boot Protocol v7.x (Higher-Half Kernel Base: `0xffffffff80000000`)
    * **Privilege Level:** Ring 0 Supervisor with Capability-Enforced Syscalls
    * **Interrupt Routing:** Dual 8259 PIC Remapped (IRQ 32-47), 100 Hz Programmable Interval Timer (PIT)
    * **Memory Management:** Bitmap Physical Memory Manager (PMM), 4-Level Paging (PML4, PDPT, PD, PT), Free-List Kernel Heap
    * **Process Model:** Preemptive Round-Robin Scheduling, Dedicated Per-Process PML4 Address Spaces
    * **Security Model:** Object-Capability Table per Process, Enforced at the Syscall Boundary
    * **Storage:** Primary Bus ATA PIO Driver (28-bit LBA), Custom INFS Filesystem
    * **Display & Input:** 32-bit Linear Framebuffer, 8x8 Monospace Bitmap Font, Scan Code Set 1 PS/2 Keyboard Driver
    
    ---
    
    ## Core Subsystems
    
    ### 1. Bootstrapping and Hardware Setup
    * **Limine Handshake:** Requests memory map, framebuffer, kernel addresses, and the higher-half direct map (HHDM).
    * **Global Descriptor Table (GDT):** Sets up null, 64-bit kernel code (`0x08`), and 64-bit kernel data (`0x10`) segments. Flushes segment registers and updates code segment via
  far return (`retfq`).
    * **Interrupt Descriptor Table (IDT):** Maps 256 interrupt gates. Handles CPU exceptions (`#DE`, `#UD`, `#GP`, `#PF`) with full register capture and visual halt screens, plus
  hardware IRQs (IRQ0 Timer, IRQ1 Keyboard) and software interrupt `int $0x80`.
    
    ### 2. Memory Subsystems
    * **Physical Memory Manager (PMM):** 
      * Scans Limine memory map entries to identify usable physical RAM.
      * Tracks page frames (4 KB each) using a global bitmap.
      * Preserves frame 0 against null allocation and marks its own bitmap memory as reserved.
    * **Paging & Higher-Half Direct Map (HHDM):**
      * Constructs a custom 4-level page table hierarchy.
      * Identity-maps physical RAM for kernel initialization, establishes a universal HHDM window (`0xffff800000000000+`) for physical address access across all address spaces, maps
  kernel code/data at `0xffffffff80000000`, allocates a dedicated 16 KB kernel stack, and maps the linear framebuffer.
      * Trampolines control into the newly active page table using `paging_switch`.
    * **Kernel Heap Allocator:**
      * Free-list memory allocator starting at virtual address `0xffffffff90000000`.
      * Implements `kmalloc()` with 8-byte alignment and block splitting, as well as `kfree()`.
    
    ### 3. Preemptive Multitasking & Address Space Isolation
    * **Process Control Block (PCB):** Each process holds a PID, execution state (`READY`, `RUNNING`, `BLOCKED`), saved stack pointer (`rsp`), capability table, and physical address
  of its own PML4 table (`pml4_phys`).
    * **Address Space Isolation:** Each process receives a unique PML4. Entries `0..255` (lower-half user memory) are zeroed and private. Entries `256..511` (higher-half kernel
  space and HHDM) are shared.
    * **Preemption:** Timer IRQ0 fires every 10 ms (100 Hz). The assembly stub (`irq0_stub`) pushes register context, invokes `schedule()`, reloads `CR3` to the next process's PML4,
  restores register state, and executes `iretq`.
    
    ### 4. Capability-Based Security Model
    Infinity OS implements an object-capability security model rather than traditional ambient authority or access control lists:
    * Processes start with zero permissions by default.
    * Privileges are represented by discrete tokens stored inside the process's capability table (`struct capability`).
    * System calls (`int $0x80`) dispatch to `syscall_handler()`. Operations such as `SYS_WRITE_PIXEL` check `process_check_capability()` before execution. If the caller lacks a
  valid capability token matching the requested coordinates, access is denied (`-1`).
    
    ### 5. Storage and Filesystem (INFS)
    * **ATA PIO Driver:** Direct port I/O communication (`0x1F0`-`0x1F7`) supporting 28-bit LBA sector reads and writes with bounded polling status detection.
    * **Disk Abstraction:** Clean generic interface (`disk_read_sector`, `disk_write_sector`) abstracting driver details.
    * **INFS Filesystem:**
      * Sector 0: Superblock storing filesystem signature (`0x494E4653` / "INFS") and file count.
      * Sectors 1-3: Fixed 32-entry directory table storing filenames, file sizes, and start sectors.
      * Data Sectors: File data allocated in dedicated sector blocks.
    
    ### 6. Video Output, Typography, and Shell
    * **Linear Framebuffer:** Double-buffered visual diagnostics and direct 32-bit RGBA pixel rendering.
    * **Bitmap Font:** Embedded 8x8 monospace font supporting digits `0-9`, upper and lowercase letters `A-Z`, `a-z`, and common punctuation.
    * **Interactive Shell:** Runs as an independent process (PID 1). Features command input, command history scrolling buffer, prompt rendering, and backspace handling.
    
    ---
    
    ## Interactive Shell Commands
    
    | Command | Description |
    | :--- | :--- |
    | `help` | Displays available shell commands. |
    | `ls` | Lists all files currently stored in the filesystem. |
    | `cat <filename>` | Reads and prints the contents of a file from disk. |
    | `write <filename>` | Enters text-entry mode to write and save content to a file. |
    | `ps` | Lists active processes, displaying their PID and execution state. |
    | `capdemo` | Launches two concurrent processes demonstrating capability enforcement (one draws, one is denied). |
    | `clear` | Clears the shell scrollback area and resets the prompt. |
    
    ---
    
    ## Project Structure
    

  infinity/
  |-- Makefile                    # Build automation and QEMU run targets
  |-- linker.ld                   # Linker script for higher-half kernel image
  |-- limine.cfg                  # Limine bootloader configuration
  |-- disk.img                    # Emulated hard disk image for ATA PIO driver
  |-- kernel/
  |-- kernel.elf              # Compiled ELF64 kernel executable
  |-- src/
  |-- kernel.c            # Kernel initialization and boot orchestration
  |-- gdt.c / gdt.h       # Global Descriptor Table setup
  |-- gdt_flush.asm       # GDT reload and segment register reload
  |-- idt.c / idt.h       # Interrupt Descriptor Table management
  |-- idt_load.asm        # LIDT wrapper
  |-- isr.asm             # CPU exception interrupt service routines
  |-- pic.c / pic.h       # 8259 PIC remapping and PIT frequency setup
  |-- irq.asm             # Hardware interrupt stubs (IRQ0 scheduler, IRQ1 keyboard)
  |-- io.h                # Inlined assembly port I/O functions
  |-- pmm.c / pmm.h       # Physical memory bitmap allocator
  |-- paging.c / paging.h # 4-level page table management and HHDM
  |-- paging_load.asm     # CR3 loading and page-table switch trampoline
  |-- heap.c / heap.h     # Kernel heap allocator (kmalloc, kfree)
  |-- process.c / process.h # PCB, process creation, capability checks, scheduler
  |-- context_switch.asm  # Assembly context switch routine
  |-- syscall.c / syscall.h # Syscall dispatcher and capability validation
  |-- syscall_entry.asm   # int 0x80 assembly entry stub
  |-- disk.c / disk.h     # Generic sector read/write disk interface
  |-- ata.c / ata.h       # ATA PIO disk controller driver
  |-- fs.c / fs.h         # INFS filesystem implementation
  |-- keyboard.c / keyboard.h # PS/2 keyboard driver and ring buffer
  |-- font.c / font.h     # 8x8 monospace bitmap font definitions
  |-- text.c / text.h     # Text, string, and hexadecimal screen blitting
  |-- shell.c / shell.h   # Interactive command shell

    
    ---
    
    ## Building and Running

    ### Prerequisites
    * `x86_64-elf-gcc` (Cross compiler targeting x86_64 freestanding)
    * `nasm` (Netwide Assembler)
    * `xorriso` (ISO creation utility)
    * `qemu-system-x86_64` (Hardware emulator)

    ### Compilation
    To compile the kernel and package the bootable ISO image:
    ```bash
    make iso

  ### Running in QEMU

  To launch Infinity OS inside QEMU with an attached hard disk:

    make run

  ### Clean Rebuild

  To remove all compiled object files, intermediate artifacts, and generated ISOs:

    make clean
  ──────
  ## Roadmap

  • Hardware Privilege Separation: Implement Ring 3 user mode, User GDT descriptors (0x1B, 0x23), and a 64-bit Task State Segment (TSS) for hardware-enforced privilege transitions.
  • Storage Enhancements: Implement dynamic sector allocation (bitmap or linked cluster table) and file deletion (rm).
  • Multi-Line Text Output: Extend file viewing and editing utilities to support multi-line buffers.
  • Graphical User Interface: Construct 2D primitives (lines, rectangles), a PS/2 mouse driver (IRQ 12), and window composition over the framebuffer.


