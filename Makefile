BUILD_DIR := build
ISO_DIR   := $(BUILD_DIR)/iso
KERNEL    := $(BUILD_DIR)/kernel.elf
ISO       := $(BUILD_DIR)/seventhos.iso
VERSION   ?= 1.0.0

CC   := gcc
LD   := ld
NASM := nasm

SRC_DIRS := kernel drivers interrupts shell
INCLUDES := $(addprefix -I,$(SRC_DIRS))

CFLAGS  := -m32 -std=gnu11 -ffreestanding -O2 -g -Wall -Wextra \
           -fno-stack-protector -fno-pie -fno-pic -MMD -MP
LDFLAGS := -m elf_i386 -T linker.ld

C_SRCS   := $(foreach d,$(SRC_DIRS),$(wildcard $(d)/*.c))
ASM_SRCS := boot/boot.asm $(wildcard interrupts/*.asm kernel/*.asm)
C_OBJS   := $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SRCS))
ASM_OBJS := $(patsubst %.asm,$(BUILD_DIR)/%.ao,$(ASM_SRCS))

TEST_DIR  := $(BUILD_DIR)/iso-test
TEST_ISO  := $(BUILD_DIR)/seventhos-test.iso
TEST_LOG  := $(BUILD_DIR)/test.log
QEMU_TEST := qemu-system-i386 -m 128M -display none -no-reboot \
             -serial file:$(TEST_LOG) \
             -device isa-debug-exit,iobase=0xf4,iosize=0x04

.PHONY: all clean run check test release debug

all: $(ISO)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/%.ao: %.asm
	@mkdir -p $(dir $@)
	$(NASM) -f elf32 $< -o $@

$(KERNEL): $(ASM_OBJS) $(C_OBJS) linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(ASM_OBJS) $(C_OBJS)
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

# ISO uji: kernel.elf yang SAMA dengan rilis, hanya argumen GRUB "selftest" yang berbeda.
$(TEST_ISO): $(KERNEL)
	mkdir -p $(TEST_DIR)/boot/grub
	cp $(KERNEL) $(TEST_DIR)/boot/kernel.elf
	printf 'set timeout=0\nset default=0\nmenuentry "seventhOS selftest" {\n    multiboot /boot/kernel.elf selftest\n    boot\n}\n' > $(TEST_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(TEST_DIR)

# Lulus = QEMU keluar dengan status 33 (selftest_exit(1)). 124 = timeout, 0 = QEMU reset (triple fault).
test: $(TEST_ISO)
	@rm -f $(TEST_LOG)
	@timeout 180 $(QEMU_TEST) -cdrom $(TEST_ISO); code=$$?; \
	 tr -d '\r' < $(TEST_LOG) | tail -n 70; \
	 if [ $$code -eq 33 ]; then echo "==> TEST PASSED"; \
	 else echo "==> TEST FAILED (qemu exit code $$code; 124 = timeout, 0 = reset)"; exit 1; fi

# Terminal 1: make debug      Terminal 2: gdb build/kernel.elf -ex "target remote localhost:1234"
debug: $(ISO)
	qemu-system-i386 -cdrom $(ISO) -m 128M -s -S

release:
	$(MAKE) clean
	$(MAKE) test
	$(MAKE) $(ISO)
	mkdir -p release
	cp $(ISO) release/seventhos-$(VERSION).iso
	cd release && sha256sum seventhos-$(VERSION).iso > seventhos-$(VERSION).iso.sha256
	@echo "Release ready: release/seventhos-$(VERSION).iso"

clean:
	rm -rf $(BUILD_DIR)

-include $(C_OBJS:.o=.d)