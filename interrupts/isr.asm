BITS 32

section .text
extern exception_handler
extern timer_irq_handler
extern syscall_handler
extern keyboard_irq_handler
extern timer_irq_handler

; Anggota 4: stub exception CPU menormalkan stack agar handler menerima vector/error.
%macro ISR_NO_ERROR 1
isr_stub_%1:
    push dword 0
    push dword %1
    jmp exception_common
%endmacro

%macro ISR_ERROR 1
isr_stub_%1:
    push dword %1
    jmp exception_common
%endmacro

%macro ENTER_KERNEL 0
    pusha
    push ds
    push es
    mov ax, 0x10            ; KERNEL_DATA_SEL
    mov ds, ax
    mov es, ax
    cld
%endmacro

%macro LEAVE_KERNEL 0
    pop es
    pop ds
    popa
%endmacro

ISR_NO_ERROR 0
ISR_NO_ERROR 1
ISR_NO_ERROR 2
ISR_NO_ERROR 3
ISR_NO_ERROR 4
ISR_NO_ERROR 5
ISR_NO_ERROR 6
ISR_NO_ERROR 7
ISR_ERROR 8
ISR_NO_ERROR 9
ISR_ERROR 10
ISR_ERROR 11
ISR_ERROR 12
ISR_ERROR 13
ISR_ERROR 14
ISR_NO_ERROR 15
ISR_NO_ERROR 16
ISR_ERROR 17
ISR_NO_ERROR 18
ISR_NO_ERROR 19
ISR_NO_ERROR 20
ISR_NO_ERROR 21
ISR_NO_ERROR 22
ISR_NO_ERROR 23
ISR_NO_ERROR 24
ISR_NO_ERROR 25
ISR_NO_ERROR 26
ISR_NO_ERROR 27
ISR_NO_ERROR 28
ISR_NO_ERROR 29
ISR_NO_ERROR 30
ISR_NO_ERROR 31

exception_common:
    ENTER_KERNEL
    mov ebx, esp
    and esp, 0xFFFFFFF0
    sub esp, 12
    push ebx                ; argumen: isr_frame_t *
    call exception_handler
    mov esp, ebx
    LEAVE_KERNEL
    add esp, 8              ; buang vector + error code
    iretd

global keyboard_irq_stub
keyboard_irq_stub:
    ENTER_KERNEL
    mov ebx, esp
    and esp, 0xFFFFFFF0
    call keyboard_irq_handler
    mov esp, ebx
    LEAVE_KERNEL
    iretd

global timer_irq_stub
timer_irq_stub:
    ENTER_KERNEL
    mov ebx, esp
    and esp, 0xFFFFFFF0
    call timer_irq_handler
    mov esp, ebx
    LEAVE_KERNEL
    iretd

; int 0x80: frame disamakan dengan exception (error=0, vector=0x80)
global syscall_stub
syscall_stub:
    push dword 0
    push dword 0x80
    ENTER_KERNEL
    mov ebx, esp
    and esp, 0xFFFFFFF0
    sub esp, 12
    push ebx
    call syscall_handler
    mov esp, ebx
    LEAVE_KERNEL
    add esp, 8
    iretd
    
section .rodata
align 4
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 32
    dd isr_stub_%+i
%assign i i+1
%endrep
