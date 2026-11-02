# RaziOS — Mini Operating System Starter

Milestone 1: boot a minimal 32-bit freestanding C kernel with GRUB Multiboot and run it in QEMU.

## 1. Install tools (Ubuntu / WSL2)

```bash
sudo apt update
sudo apt install -y build-essential gcc-multilib nasm \
  qemu-system-x86 grub-common grub-pc-bin xorriso make binutils
```

## 2. Build

Run these commands from the project root:

```bash
make clean
make
```

The ISO will be created at `build/razios.iso`.

## 3. Run in QEMU

```bash
make run
```

Close QEMU with `Ctrl+C` in the terminal or close its window.

## 4. Check the kernel

```bash
make check
file build/kernel.elf
```

## Team ownership proposal

- Member 1: `boot/`, `linker.ld`, `Makefile`, GRUB and ISO build.
- Member 2: `kernel/` and terminal output.
- Member 3: `drivers/` and `shell/` (to be added in later milestones).
- Member 4: `interrupts/` and `memory/` (to be added in later milestones).

## Important

This is an educational starter, not a complete operating system. It currently boots,
writes to VGA text memory, and halts. Keyboard input, IDT/ISR, memory allocation,
and shell commands are intentionally not implemented yet.

Do not let multiple team members edit the same files without agreeing on interfaces.
Use feature branches and pull requests for integration.
