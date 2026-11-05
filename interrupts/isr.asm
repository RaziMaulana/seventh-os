BITS 32

section .text
extern exception_handler
extern keyboard_irq_handler

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
    pusha
    cld
    mov ebx, esp
    and esp, 0xFFFFFFF0
    sub esp, 8              ; padding agar stack C sejajar 16 byte setelah argumen
    push dword [ebx + 36]   ; error code
    push dword [ebx + 32]   ; vector number
    call exception_handler
    add esp, 16
    mov esp, ebx
    popa
    add esp, 8
    iretd

; Anggota 4: IRQ keyboard mengikuti jalur PIC, mengirim EOI di driver, lalu iretd.
global keyboard_irq_stub
keyboard_irq_stub:
    pusha
    cld
    mov ebx, esp
    and esp, 0xFFFFFFF0
    call keyboard_irq_handler
    mov esp, ebx
    popa
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
