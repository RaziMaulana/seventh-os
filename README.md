# SeventhOS — Mini Operating System

SeventhOS is an educational 32-bit x86 operating system developed using C and Assembly. This project aims to introduce fundamental operating system concepts, starting from the boot process to a basic command-line shell.

The project uses GRUB Multiboot as its bootloader, NASM for assembly, GCC for C compilation, GNU LD for linking, and QEMU for emulation.

## 1. Requirements

### Development Environment

- Ubuntu / WSL2
- Visual Studio Code
- GCC and GNU Binutils
- NASM
- GRUB
- QEMU
- Make

### Install Dependencies

```bash
sudo apt update

sudo apt install -y \
  build-essential \
  gcc-multilib \
  nasm \
  qemu-system-x86 \
  grub-common \
  grub-pc-bin \
  xorriso \
  make \
  binutils
```

Verify the installation:

```bash
gcc --version
nasm --version
make --version
qemu-system-i386 --version
```

## 2. Project Structure

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

## 3. Development Milestones

# Milestone 1 — Bootloader and Kernel Initialization

### Objective

Create a minimal 32-bit operating system that can boot through GRUB and execute a freestanding C kernel.

### Concepts

- BIOS and boot process fundamentals
- GRUB Multiboot
- x86 Assembly
- Freestanding C
- Linker scripts
- Kernel entry point

### Step 1: Create the Bootloader

Create `boot/boot.asm`.

This file contains the Multiboot header and the initial assembly instructions that prepare the stack before transferring execution to the C kernel.

```asm
section .multiboot
align 4

MB_MAGIC    equ 0x1BADB002
MB_FLAGS    equ 0x00000003
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)

dd MB_MAGIC
dd MB_FLAGS
dd MB_CHECKSUM

section .bss
align 16

stack_bottom:
    resb 16384
stack_top:

section .text
global _start
extern kernel_main

_start:
    mov esp, stack_top

    call kernel_main

.hang:
    cli
    hlt
    jmp .hang
```

### Step 2: Create the Kernel

Create `kernel/kernel.c`.

The kernel is the core component of the operating system. For the first milestone, it only needs to initialize and display a simple message.

```c
void kernel_main(void)
{
    volatile char *video_memory = (volatile char *)0xB8000;

    const char *message = "Welcome to SeventhOS!";

    for (int i = 0; message[i] != '\0'; i++)
    {
        video_memory[i * 2] = message[i];
        video_memory[i * 2 + 1] = 0x0F;
    }

    while (1)
    {
        __asm__ volatile("hlt");
    }
}
```

The address `0xB8000` refers to VGA text-mode memory. Each character occupies two bytes: one for the character and another for its display attributes.

### Step 3: Create the Linker Script

Create `linker.ld`.

```ld
ENTRY(_start)

SECTIONS
{
    . = 1M;

    .text ALIGN(4K) :
    {
        *(.multiboot)
        *(.text)
    }

    .rodata ALIGN(4K) :
    {
        *(.rodata)
    }

    .data ALIGN(4K) :
    {
        *(.data)
    }

    .bss ALIGN(4K) :
    {
        *(COMMON)
        *(.bss)
    }
}
```

The linker script defines the memory layout of the kernel and specifies `_start` as its entry point.

### Step 4: Configure GRUB

Create `iso/boot/grub/grub.cfg`.

```cfg
set timeout=0
set default=0

menuentry "SeventhOS" {
    multiboot /boot/kernel.elf
    boot
}
```

### Expected Result

After building and running the ISO, QEMU should display:

```text
Welcome to SeventhOS!
```

---

# Milestone 2 — VGA Text Mode and Terminal Management

### Objective

Create a terminal management system that allows the kernel to display text, manage colors, clear the screen, and handle basic character operations.

### Concepts

- VGA text mode
- Video memory
- Character attributes
- Terminal cursor
- Screen scrolling
- Header and source files in C

### Step 1: Create the Terminal Header

Create `kernel/terminal.h`.

```c
#ifndef SEVENTHOS_TERMINAL_H
#define SEVENTHOS_TERMINAL_H

#include <stdint.h>

enum terminal_color
{
    COLOR_BLACK = 0,
    COLOR_BLUE = 1,
    COLOR_GREEN = 2,
    COLOR_CYAN = 3,
    COLOR_RED = 4,
    COLOR_MAGENTA = 5,
    COLOR_BROWN = 6,
    COLOR_LIGHT_GREY = 7,
    COLOR_DARK_GREY = 8,
    COLOR_LIGHT_BLUE = 9,
    COLOR_LIGHT_GREEN = 10,
    COLOR_LIGHT_CYAN = 11,
    COLOR_LIGHT_RED = 12,
    COLOR_LIGHT_MAGENTA = 13,
    COLOR_YELLOW = 14,
    COLOR_WHITE = 15
};

void terminal_initialize(void);
void terminal_clear(void);
void terminal_set_color(uint8_t foreground, uint8_t background);
void terminal_putchar(char character);
void terminal_write(const char *text);

#endif
```

### Step 2: Implement Terminal Functions

Create `kernel/terminal.c`.

The terminal uses VGA memory at `0xB8000`, with an 80-column by 25-row display.

Implement the following functions:

- `terminal_initialize()` — initialize terminal state.
- `terminal_clear()` — clear the display.
- `terminal_set_color()` — configure foreground and background colors.
- `terminal_putchar()` — display one character.
- `terminal_write()` — display a string.
- Handle newline, backspace, and scrolling.

A VGA character entry can be constructed using:

```c
static uint8_t terminal_color =
    COLOR_LIGHT_GREY | (COLOR_BLACK << 4);

static uint16_t make_vga_entry(char character, uint8_t color)
{
    return (uint16_t)character | ((uint16_t)color << 8);
}
```

Use a volatile pointer to access video memory:

```c
static volatile uint16_t *const VGA_MEMORY =
    (volatile uint16_t *)0xB8000;
```

The terminal should maintain its own cursor position and update it whenever a character is printed or a newline is encountered.

### Step 3: Integrate the Terminal into the Kernel

Update `kernel/kernel.c`.

Instead of writing directly to VGA memory, use the terminal functions:

```c
#include "terminal.h"

void kernel_main(void)
{
    terminal_initialize();

    terminal_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    terminal_write("Welcome to SeventhOS!\n");

    terminal_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
    terminal_write("[ OK ] Terminal initialized\n");

    while (1)
    {
        __asm__ volatile("hlt");
    }
}
```

### Expected Result

The kernel should display text using the terminal management system, support multiple colors, clear the screen, and handle basic cursor movement and scrolling.

---

# Milestone 3 — Interrupt Management and Keyboard Driver

### Objective

Implement interrupt management and a keyboard driver so users can interact with the operating system through keyboard input.

### Concepts

- Interrupt Descriptor Table (IDT)
- Interrupt Service Routine (ISR)
- Programmable Interrupt Controller (PIC)
- IRQ
- Keyboard scancodes
- Ring buffer
- Interrupt handling

### Step 1: Create the IDT Header

Create `interrupts/idt.h`.

```c
#ifndef SEVENTHOS_IDT_H
#define SEVENTHOS_IDT_H

void idt_initialize(void);

#endif
```

### Step 2: Implement the IDT

Create `interrupts/idt.c`.

The IDT contains 256 entries. Each entry describes how the CPU should handle a specific interrupt.

The implementation should include:

1. An IDT entry structure.
2. An IDT pointer structure.
3. A function to configure an IDT entry.
4. PIC remapping.
5. IDT loading using the `lidt` instruction.

For this milestone, keyboard IRQ1 is mapped to interrupt vector `0x21`.

PIC remapping uses:

- Master PIC offset: `0x20`
- Slave PIC offset: `0x28`

Unmask IRQ1 on the master PIC so keyboard interrupts can be received.

### Step 3: Create the Interrupt Service Routine

Create `interrupts/isr.asm`.

The keyboard ISR should preserve CPU registers, call the keyboard handler, and return from the interrupt.

```asm
global keyboard_isr
extern keyboard_irq_handler

keyboard_isr:
    pusha
    cld

    call keyboard_irq_handler

    popa
    iretd
```

The ISR is responsible for transferring control to the C keyboard handler.

### Step 4: Create the Keyboard Header

Create `drivers/keyboard.h`.

```c
#ifndef SEVENTHOS_KEYBOARD_H
#define SEVENTHOS_KEYBOARD_H

void keyboard_initialize(void);
void keyboard_irq_handler(void);
char keyboard_getchar(void);

#endif
```

### Step 5: Implement the Keyboard Driver

Create `drivers/keyboard.c`.

The keyboard driver should:

1. Read keyboard scancodes from I/O port `0x60`.
2. Translate set-1 scancodes into ASCII characters.
3. Handle Shift and Backspace.
4. Store input in a ring buffer.
5. Provide `keyboard_getchar()` to retrieve characters.
6. Send an End of Interrupt (EOI) signal to the master PIC after handling IRQ1.

Example for reading a scancode:

```c
static inline unsigned char inb(unsigned short port)
{
    unsigned char result;

    __asm__ volatile(
        "inb %1, %0"
        : "=a"(result)
        : "Nd"(port)
    );

    return result;
}
```

The actual driver should also distinguish key press and key release events, and avoid inserting release scancodes into the input buffer.

### Step 6: Enable Interrupts

Update `kernel/kernel.c`.

After initializing the IDT and keyboard driver, enable CPU interrupts:

```c
keyboard_initialize();
idt_initialize();

__asm__ volatile("sti");
```

The order and interrupt configuration must be consistent with the IDT and PIC implementation.

### Expected Result

The operating system should be able to receive keyboard input without rebooting or crashing. Characters should be available through `keyboard_getchar()`.

---

# Milestone 4 — Shell and Command Interpreter

### Objective

Develop a basic command-line shell that allows users to execute built-in commands.

### Concepts

- Input buffer
- String manipulation
- Command parsing
- Command execution
- Shell loop
- Function-based command handling

### Step 1: Create the Shell Header

Create `shell/shell.h`.

```c
#ifndef SEVENTHOS_SHELL_H
#define SEVENTHOS_SHELL_H

void shell_initialize(void);
void shell_update(void);

#endif
```

### Step 2: Implement the Shell

Create `shell/shell.c`.

The shell receives keyboard input, stores it in a buffer, and executes the command when the user presses Enter.

The shell should maintain:

- An input buffer.
- The current input length.
- A prompt.
- A command interpreter.
- A command execution function.

Example prompt:

```c
static void shell_prompt(void)
{
    terminal_set_color(COLOR_YELLOW, COLOR_BLACK);
    terminal_write("seventh> ");

    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
}
```

### Step 3: Implement Built-in Commands

The initial shell should support:

| Command | Description |
|---|---|
| `help` | Display available commands |
| `clear` | Clear the terminal |
| `about` | Display operating system information |
| `version` | Display the current OS version |
| `echo` | Print a message |

Example command interpreter:

```c
static void shell_execute(const char *command)
{
    if (string_compare(command, "help"))
    {
        shell_help();
    }
    else if (string_compare(command, "clear"))
    {
        terminal_clear();
    }
    else if (string_compare(command, "about"))
    {
        terminal_write("SeventhOS is an educational 32-bit OS.\n");
    }
    else if (string_compare(command, "version"))
    {
        terminal_write("SeventhOS v0.4\n");
    }
    else
    {
        terminal_write("Command not found.\n");
    }
}
```

`string_compare()` and `shell_help()` must be implemented in `shell.c`. The `echo` command can be added by parsing the text that follows the command name.

### Step 4: Handle User Input

The shell should continuously retrieve characters from `keyboard_getchar()`.

```c
void shell_update(void)
{
    char character = keyboard_getchar();

    if (character == 0)
        return;

    if (character == '\b')
    {
        if (input_length > 0)
        {
            terminal_putchar('\b');
            input_length--;
            input_buffer[input_length] = '\0';
        }
    }
    else if (character == '\n' || character == '\r')
    {
        terminal_putchar('\n');

        input_buffer[input_length] = '\0';

        shell_execute(input_buffer);

        input_length = 0;
        input_buffer[0] = '\0';

        shell_prompt();
    }
    else if (character >= 32 && character <= 126)
    {
        if (input_length < INPUT_BUFFER_SIZE - 1)
        {
            input_buffer[input_length++] = character;
            input_buffer[input_length] = '\0';

            terminal_putchar(character);
        }
    }
}
```

This example assumes `input_buffer`, `input_length`, and `INPUT_BUFFER_SIZE` are declared in `shell.c`.

### Step 5: Integrate the Shell into the Kernel

Update `kernel/kernel.c`.

```c
#include "terminal.h"
#include "idt.h"
#include "keyboard.h"
#include "shell.h"

void kernel_main(void)
{
    terminal_initialize();

    terminal_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    terminal_write("========================================\n");
    terminal_write("              SeventhOS v0.4            \n");
    terminal_write("========================================\n\n");

    keyboard_initialize();
    idt_initialize();

    __asm__ volatile("sti");

    shell_initialize();

    while (1)
    {
        shell_update();
    }
}
```

### Expected Result

The user should be able to interact with SeventhOS through a command-line interface.

Example:

```text
========================================
              SeventhOS v0.4
========================================

seventh> help

Available commands:
  help       Show available commands
  clear      Clear the terminal
  about      Show OS information
  version    Show OS version
  echo       Print a message

seventh> echo Hello World
Hello World

seventh> about
SeventhOS is an educational 32-bit OS.

seventh>
```

---

## 4. Build Configuration

The Makefile must compile all source files and link them into the kernel.

The following is the required build configuration structure. Add or update the object-file targets whenever new source files are introduced.

```makefile
BUILD_DIR := build
ISO_DIR := $(BUILD_DIR)/iso
KERNEL := $(BUILD_DIR)/kernel.elf
ISO := $(BUILD_DIR)/seventhos.iso

CC := gcc
LD := ld
NASM := nasm

CFLAGS := -m32 -std=gnu11 -ffreestanding -O2 \
          -Wall -Wextra -fno-stack-protector \
          -fno-pie -fno-pic

LDFLAGS := -m elf_i386 -T linker.ld

INCLUDES := -Ikernel -Iinterrupts -Idrivers -Ishell
```

The build must include object files from:

- `boot/boot.asm`
- `interrupts/isr.asm`
- `kernel/kernel.c`
- `kernel/terminal.c`
- `interrupts/idt.c`
- `drivers/keyboard.c`
- `shell/shell.c`

The linker combines these object files into `kernel.elf`. GRUB then packages the kernel into a bootable ISO.

## 5. Build and Run

Run the following commands from the project root.

### Clean Previous Build

```bash
make clean
```

### Build the Operating System

```bash
make
```

The ISO will be generated at:

```text
build/seventhos.iso
```

### Run in QEMU

```bash
make run
```

### Check the Kernel

```bash
make check
file build/kernel.elf
```

## 6. Team Ownership Proposal

| Member | Responsibility |
|---|---|
| Member 1 | `boot/`, `linker.ld`, `Makefile`, GRUB and ISO build |
| Member 2 | `kernel/` and terminal management |
| Member 3 | `drivers/` and `shell/` |
| Member 4 | `interrupts/` and future `memory/` development |

Team members should agree on interfaces before integrating their work. Use feature branches and pull requests to avoid conflicting changes.

## 7. Future Development

After completing the first four milestones, SeventhOS can be expanded with:

- Physical memory management
- Heap allocation
- Filesystem support
- Process management
- System calls
- Multitasking
- Additional shell commands

These features are not part of the first four milestones.

## 8. Important Notes

SeventhOS is an educational operating system project, not a production-ready operating system.

Each milestone builds on the previous one. Make sure that the current milestone works before moving to the next stage.

The code snippets in this guide illustrate the main implementation steps. Some components, particularly the IDT, PIC, keyboard translation, ring buffer, terminal scrolling, and complete Makefile rules, require full implementations in their respective source files before the project can build and run successfully.
