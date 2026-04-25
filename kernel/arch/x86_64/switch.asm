BITS 64
default rel

global x64_process_switch
global x64_process_bootstrap_trampoline

extern process_bootstrap_entry

section .text
x64_process_switch:
    cli
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rdi], rsp
    mov rsp, rsi
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    sti
    ret

x64_process_bootstrap_trampoline:
    xor rbp, rbp
    call process_bootstrap_entry

.halt:
    cli
    hlt
    jmp .halt
