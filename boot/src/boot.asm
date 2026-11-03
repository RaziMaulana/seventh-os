[BITS 16]
[ORG 0x7c00]

start:
    cli ;Clear interrupts
    mov ax, 0x00
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    sti ;Enable interrupts
    mov si, msg
    
print:
    lodsb ;Loads byte at ds:si to AL register and incremens SI
    cmp al, 0 ;Check if AL is 0 (end of string)
    je done ;If AL is 0, jump to done
    mov ah, 0x0E
    int 0x10 ;Call BIOS interrupt to print character in AL
    jmp print ;Jump back to print

done:
    cli
    hlt ;Stop further CPU execution

msg: db 'Hello World!', 0

times 510 - ($ - $$) db 0 ;Fill the rest of the boot sector with zeros

dw 0xAA55