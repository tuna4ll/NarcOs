BITS 64
default rel

global _start
extern __bss_start
extern __bss_end
extern kmain

section .bss
align 16
boot_stack:
    resb 16384
boot_stack_top:

section .text.boot
_start:
    cld
    lea rdi, [rel __bss_start]
    lea rcx, [rel __bss_end]
    sub rcx, rdi
    xor eax, eax
    rep stosb

    lea rsp, [rel boot_stack_top]
    xor rbp, rbp
    call kmain

.halt:
    cli
    hlt
    jmp .halt
