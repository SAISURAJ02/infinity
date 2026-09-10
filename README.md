# Infinity

A hobby operating system built from scratch in C and x86 assembly.

## Status
🚧 Just getting started — environment setup in progress.

## Goals
- 32-bit x86 kernel, booted via GRUB/Multiboot
- Memory management (paging, kernel heap)
- Preemptive multitasking with a basic scheduler
- Simple filesystem and shell

## Building
(instructions coming soon)
EOF


## Dependencies

This project uses the [Limine bootloader](https://github.com/limine-bootloader/limine). To set it up:

```bash
git clone https://github.com/limine-bootloader/limine.git --branch=v7.x-binary --depth=1
cd limine && make && cd ..
```
