BITS 32

GLOBAL _start
EXTERN main

%define SYS_EXIT 0

SECTION .text
_start:
    mov eax, [esp]
    lea ebx, [esp + 4]
    push ebx
    push eax
    call main

    mov ebx, eax
    mov eax, SYS_EXIT
    int 0x80

.hang:
    int 0x81
    jmp .hang
