BITS 64
default rel

GLOBAL _start
EXTERN main

%define SYS_EXIT 0

SECTION .text
_start:
    xor ebp, ebp
    mov rdi, [rsp]
    lea rsi, [rsp + 8]
    and rsp, -16
    call main

    mov rbx, rax
    mov eax, SYS_EXIT
    int 0x80

.hang:
    int 0x81
    jmp .hang
