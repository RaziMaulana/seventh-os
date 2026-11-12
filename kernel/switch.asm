BITS 32

section .text
global context_switch

; void context_switch(uint32_t *old_esp, uint32_t new_esp)
; Simpan konteks callee-saved thread lama di stack-nya, simpan esp ke *old_esp,
; pindah ke stack thread baru, pulihkan konteksnya, lalu "ret" ke alamat
; yang menunggu di stack tersebut.
context_switch:
    mov eax, [esp + 4]      ; old_esp
    mov edx, [esp + 8]      ; new_esp
    push ebp
    push ebx
    push esi
    push edi
    mov [eax], esp
    mov esp, edx
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret