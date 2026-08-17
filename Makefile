# Infinity OS Makefile

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
