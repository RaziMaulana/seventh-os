BUILD_DIR := build
ISO_DIR := $(BUILD_DIR)/iso
KERNEL := $(BUILD_DIR)/kernel.elf
ISO := $(BUILD_DIR)/seventhos.iso

CC := gcc
LD := ld
NASM := nasm

CFLAGS := -m32 -std=gnu11 -ffreestanding -O2 -Wall -Wextra \
          -fno-stack-protector -fno-pie -fno-pic
LDFLAGS := -m elf_i386 -T linker.ld
INCLUDES := -Ikernel -Iinterrupts -Idrivers -Ishell

.PHONY: all clean run check

all: $(ISO)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/boot.o: boot/boot.asm | $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@

$(BUILD_DIR)/keyboard.o: drivers/keyboard.c drivers/keyboard.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Idrivers -c $< -o $@

$(BUILD_DIR)/shell.o: shell/shell.c shell/shell.h drivers/keyboard.h kernel/terminal.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/terminal.o: kernel/terminal.c kernel/terminal.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/memory.o: kernel/memory.c kernel/memory.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD_DIR)/idt.o: interrupts/idt.c interrupts/idt.h kernel/terminal.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/isr.o: interrupts/isr.asm | $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@

$(BUILD_DIR)/kernel.o: kernel/kernel.c kernel/memory.h kernel/terminal.h interrupts/idt.h drivers/keyboard.h shell/shell.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(KERNEL): $(BUILD_DIR)/boot.o $(BUILD_DIR)/kernel.o $(BUILD_DIR)/terminal.o $(BUILD_DIR)/keyboard.o $(BUILD_DIR)/shell.o $(BUILD_DIR)/memory.o $(BUILD_DIR)/idt.o $(BUILD_DIR)/isr.o linker.ld
	$(LD) $(LDFLAGS) -o $@ $(BUILD_DIR)/boot.o $(BUILD_DIR)/kernel.o $(BUILD_DIR)/terminal.o $(BUILD_DIR)/keyboard.o $(BUILD_DIR)/shell.o $(BUILD_DIR)/memory.o $(BUILD_DIR)/idt.o $(BUILD_DIR)/isr.o
	grub-file --is-x86-multiboot $@

$(ISO): $(KERNEL) iso/boot/grub/grub.cfg
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL) $(ISO_DIR)/boot/kernel.elf
	cp iso/boot/grub/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) $(ISO_DIR)

check: $(KERNEL)
	grub-file --is-x86-multiboot $(KERNEL)
	@echo "Kernel Multiboot check passed."

run: $(ISO)
	qemu-system-i386 -cdrom $(ISO) -m 128M

clean:
	rm -rf $(BUILD_DIR)
