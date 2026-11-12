BITS 32

section .text
global enter_user_mode

; void enter_user_mode(uint32_t entry, uint32_t user_esp)   -- tidak kembali
; Membentuk frame iretd: SS, ESP, EFLAGS, CS, EIP. CPU melihat CS.RPL=3 > CPL=0,
; sehingga berpindah ke Ring 3 dan memuat SS:ESP dari frame.
enter_user_mode:
    mov ecx, [esp + 4]          ; entry
    mov edx, [esp + 8]          ; user esp
    mov ax, 0x23                ; USER_DATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push dword 0x23             ; SS
    push edx                    ; ESP
    pushfd
    pop eax
    or  eax, 0x200              ; IF = 1: timer tetap bisa mem-preempt kode user
    and eax, 0xFFFFCFFF         ; IOPL = 0: in/out dilarang di Ring 3
    push eax                    ; EFLAGS
    push dword 0x1B             ; CS = USER_CODE_SEL
    push ecx                    ; EIP
    iretd