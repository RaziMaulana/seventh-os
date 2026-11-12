BITS 32

SYS_EXIT   equ 1
SYS_WRITE  equ 2
SYS_GETPID equ 3
SYS_YIELD  equ 4

section .rodata
align 16

; ---- hello: akses yang DIIZINKAN. Syscall write + exit(code = pid). ----
global user_hello_start, user_hello_end
user_hello_start:
    call .ip
.ip:
    pop esi
    mov eax, SYS_GETPID
    int 0x80
    mov edi, eax
    mov eax, SYS_WRITE
    lea ebx, [esi + (.msg - .ip)]
    mov ecx, .msg_end - .msg
    int 0x80
    mov eax, SYS_EXIT
    mov ebx, edi
    int 0x80
.hang:
    jmp .hang
.msg:
    db "[user] Hello from Ring 3! (syscall via int 0x80)", 10
.msg_end:
user_hello_end:

align 16
; ---- badptr: syscall dengan pointer kernel -> ditolak kernel, proses tetap hidup. ----
global user_badptr_start, user_badptr_end
user_badptr_start:
    call .ip
.ip:
    pop esi
    mov eax, SYS_WRITE
    mov ebx, 0x00100000          ; alamat kernel
    mov ecx, 32
    int 0x80
    cmp eax, -1
    jne .unexpected
    mov eax, SYS_WRITE
    lea ebx, [esi + (.ok - .ip)]
    mov ecx, .ok_end - .ok
    int 0x80
    mov eax, SYS_EXIT
    xor ebx, ebx
    int 0x80
.unexpected:
    mov eax, SYS_EXIT
    mov ebx, 99
    int 0x80
.ok:
    db "[user] kernel rejected my bad pointer (syscall returned -1)", 10
.ok_end:
user_badptr_end:

align 16
; ---- kwrite: menulis ke memori kernel -> page fault proteksi. ----
global user_kwrite_start, user_kwrite_end
user_kwrite_start:
    call .ip
.ip:
    pop esi
    mov eax, SYS_WRITE
    lea ebx, [esi + (.msg - .ip)]
    mov ecx, .msg_end - .msg
    int 0x80
    mov dword [0x00100000], 0xDEADBEEF
    mov eax, SYS_EXIT
    xor ebx, ebx
    int 0x80
.msg:
    db "[user] writing to kernel memory at 0x00100000...", 10
.msg_end:
user_kwrite_end:

align 16
; ---- rowrite: menulis ke halaman kodenya sendiri (read-only) -> page fault. ----
global user_rowrite_start, user_rowrite_end
user_rowrite_start:
    call .ip
.ip:
    pop esi
    mov eax, SYS_WRITE
    lea ebx, [esi + (.msg - .ip)]
    mov ecx, .msg_end - .msg
    int 0x80
    mov dword [esi], 0
    mov eax, SYS_EXIT
    xor ebx, ebx
    int 0x80
.msg:
    db "[user] writing to my own read-only code page...", 10
.msg_end:
user_rowrite_end:

align 16
; ---- priv: instruksi privileged di Ring 3 -> #GP. ----
global user_priv_start, user_priv_end
user_priv_start:
    call .ip
.ip:
    pop esi
    mov eax, SYS_WRITE
    lea ebx, [esi + (.msg - .ip)]
    mov ecx, .msg_end - .msg
    int 0x80
    hlt
    mov eax, SYS_EXIT
    xor ebx, ebx
    int 0x80
.msg:
    db "[user] executing privileged instruction HLT...", 10
.msg_end:
user_priv_end:

align 16
; ---- spin: loop tak berujung di Ring 3 (bukti preemption timer pada kode user). ----
global user_spin_start, user_spin_end
user_spin_start:
    call .ip
.ip:
    pop esi
    mov eax, SYS_WRITE
    lea ebx, [esi + (.msg - .ip)]
    mov ecx, .msg_end - .msg
    int 0x80
.loop:
    inc edi
    jmp .loop
.msg:
    db "[user] spinning in Ring 3 (stop with: kill <thread id>)", 10
.msg_end:
user_spin_end: