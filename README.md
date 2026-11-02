# SeventhOS — Mini Operating System

A minimal 32-bit operating system built from scratch using C and x86 Assembly.

This project is developed incrementally through four milestones, starting from a bootable kernel and progressing toward an interactive command-line shell.

## 1. Install tools (Ubuntu / WSL2)

Update Ubuntu package repositories:

```bash
sudo apt update
```

Install the required development tools:

```bash
sudo apt install -y build-essential gcc-multilib nasm \
  qemu-system-x86 grub-common grub-pc-bin xorriso \
  make binutils git
```

Verify the installed tools:

```bash
gcc --version
nasm --version
qemu-system-i386 --version
grub-mkrescue --version
```

## 2. Project structure

```text
SeventhOS/
├── boot/
│   └── boot.asm
├── kernel/
│   ├── kernel.c
│   ├── terminal.c
│   └── terminal.h
├── interrupts/
│   ├── idt.c
│   ├── idt.h
│   └── isr.asm
├── drivers/
│   ├── keyboard.c
│   └── keyboard.h
├── shell/
│   ├── shell.c
│   └── shell.h
├── iso/
│   └── boot/
│       └── grub/
│           └── grub.cfg
├── linker.ld
└── Makefile
```

## 3. Development milestones

### Milestone 1: Bootloader and Kernel Initialization

**Objective:** Boot a minimal 32-bit freestanding C kernel using GRUB Multiboot and execute it in QEMU.

Implementation:

- Create a Multiboot-compatible bootloader using NASM.
- Prepare the kernel entry point and stack.
- Implement a minimal `kernel_main()` function in C.
- Configure the kernel memory layout using `linker.ld`.
- Configure GRUB to load the kernel.

Expected result:

- GRUB successfully loads the kernel.
- The kernel executes in 32-bit protected mode.
- The kernel can display text through VGA text memory at `0xB8000`.

### Milestone 2: VGA Text Mode and Terminal Management

**Objective:** Implement a reusable terminal subsystem for text output and screen management.

Implementation:

- Create `kernel/terminal.c` and `kernel/terminal.h`.
- Implement VGA text buffer access.
- Support an 80 × 25 terminal display.
- Implement foreground and background colors.
- Implement character and string output.
- Handle newline and carriage return.
- Implement Backspace.
- Implement terminal scrolling and screen clearing.

Available terminal functions:

| Function | Description |
|---|---|
| `terminal_initialize()` | Initialize the terminal. |
| `terminal_clear()` | Clear the terminal screen. |
| `terminal_set_color()` | Set foreground and background colors. |
| `terminal_putchar()` | Display a single character. |
| `terminal_write()` | Display a string. |

Expected result:

- The kernel can display formatted text.
- Terminal operations are managed through reusable functions.
- Screen clearing, character output, and scrolling work correctly.

### Milestone 3: Interrupt Management and Keyboard Driver

**Objective:** Implement hardware interrupt handling and keyboard input processing.

Implementation:

- Create an Interrupt Descriptor Table (IDT) with 256 entries.
- Configure the keyboard interrupt at vector `0x21`.
- Implement Interrupt Service Routines (ISR) using NASM.
- Remap the Programmable Interrupt Controller (PIC).
- Configure IRQ1 for keyboard input.
- Read keyboard scancodes from I/O port `0x60`.
- Translate scancodes into ASCII characters.
- Implement a keyboard input buffer.
- Handle Shift, Backspace, and Enter.
- Integrate keyboard input with the terminal.

Expected result:

- Keyboard input is received through hardware interrupts.
- Pressed keys are translated into characters.
- Characters can be displayed in the terminal.
- Backspace and Enter work correctly.

### Milestone 4: Shell and Command Interpreter

**Objective:** Transform the terminal into an interactive command-line interface.

Implementation:

- Create `shell/shell.c` and `shell/shell.h`.
- Implement an input buffer with a maximum capacity of 128 characters.
- Handle character insertion and deletion.
- Process input when Enter is pressed.
- Implement basic command parsing.
- Execute built-in commands.
- Display a new prompt after each command.

Available commands:

| Command | Description |
|---|---|
| `help` | Display available commands. |
| `clear` | Clear the terminal screen. |
| `about` | Display operating system information. |
| `version` | Display the operating system version. |
| `echo` | Print the provided message. |

Example:

```text
========================================
               SeventhOS v0.3
========================================

SeventhOS Shell v0.1
Type 'help' to see available commands.

seventh> help

Available commands:
  help       Show available commands
  clear      Clear the terminal
  about      Show OS information
  version    Show OS version
  echo       Print a message

seventh> echo Hello World
Hello World

seventh> version
SeventhOS v0.3

seventh>
```

Expected result:

- Users can enter commands through the keyboard.
- The shell can recognize and execute supported commands.
- Unknown commands display an appropriate error message.
- The input buffer is reset after each command.

## 4. Build

Run these commands from the project root:

```bash
make clean
make
```

The ISO will be created at:

```text
build/seventhos.iso
```

## 5. Run in QEMU

Start SeventhOS:

```bash
make run
```

Close QEMU using `Ctrl+C` in the terminal or close its window.

## 6. Check the kernel

Validate the Multiboot kernel:

```bash
make check
```

Inspect the compiled kernel:

```bash
file build/kernel.elf
```

## 7. Team ownership proposal

- **Member 1:** `boot/`, `linker.ld`, `Makefile`, GRUB configuration, and ISO build.
- **Member 2:** `kernel/`, terminal management, and kernel integration.
- **Member 3:** `drivers/` and `shell/`, including keyboard input and command interpretation.
- **Member 4:** `interrupts/`, including IDT, ISR, and PIC configuration. Future responsibility may include `memory/`.

Team members should agree on function interfaces before integrating components.

## 8. Future development

Potential development areas after Milestone 4:

- Memory management and allocation.
- Basic filesystem implementation.
- Process management and scheduling.
- System call interface.
- User-mode execution.

These components are planned for future development and are not part of the initial four milestones.

## Important

SeventhOS is an educational operating system project, not a complete or production-ready operating system.

The first four milestones establish the foundation for booting a kernel, managing terminal output, handling keyboard interrupts, and executing basic shell commands.

Some operating system features, including memory management, filesystem support, multitasking, and user-mode execution, have not yet been implemented.

Do not let multiple team members edit the same files without agreeing on interfaces.

Use feature branches and pull requests for integration.
