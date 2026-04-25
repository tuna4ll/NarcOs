BITS 64
default rel

global x64_isr_default
global x64_isr_invalid_opcode
global x64_isr_double_fault
global x64_isr_gpf
global x64_isr_page_fault
global x64_irq_timer
global x64_irq_keyboard
global x64_irq_mouse
global x64_isr_syscall
global x64_isr_user_yield
global x64_run_user_task

global x64_test_invalid_opcode
global x64_test_invalid_opcode_resume
global x64_test_page_fault
global x64_test_page_fault_resume
global x64_test_gpf
global x64_test_gpf_resume

extern x64_interrupt_dispatch
extern syscall_handler
extern user_yield_handler
extern user_kernel_resume_esp
extern user_kernel_return_mode
extern user_current_task_frame_ptr

%define X64_FRAME_GPR_SIZE 120
%define X64_FRAME_META_SIZE 56
%define X64_GDT_KERNEL_DATA 0x10

%define TF_VECTOR    0
%define TF_ERROR     8
%define TF_RIP       16
%define TF_CS        24
%define TF_RFLAGS    32
%define TF_USER_RSP  40
%define TF_USER_SS   48
%define TF_R15       56
%define TF_R14       64
%define TF_R13       72
%define TF_R12       80
%define TF_R11       88
%define TF_R10       96
%define TF_R9        104
%define TF_R8        112
%define TF_RBP       120
%define TF_RDI       128
%define TF_RSI       136
%define TF_RDX       144
%define TF_RCX       152
%define TF_RBX       160
%define TF_RAX       168
%define TF_SIZE      176

%macro PUSH_GPRS 0
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro POP_GPRS 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
%endmacro

%macro CALL_DISPATCH 0
    cld
    mov rdi, rsp
    mov rax, rsp
    and rax, 0xF
    jz %%aligned
    sub rsp, 8
    call x64_interrupt_dispatch
    add rsp, 8
    jmp %%done
%%aligned:
    call x64_interrupt_dispatch
%%done:
%endmacro

%macro ISR_NOERR 2
%1:
    PUSH_GPRS
    lea rax, [rsp + X64_FRAME_GPR_SIZE]
    push qword 0
    lea rbx, [rax + 24]
    push rbx
    push qword [rax + 16]
    push qword [rax + 8]
    push qword [rax + 0]
    push qword 0
    push qword %2
    CALL_DISPATCH
    lea rax, [rsp + X64_FRAME_META_SIZE + X64_FRAME_GPR_SIZE]
    mov rbx, [rsp + 16]
    mov [rax + 0], rbx
    mov rbx, [rsp + 24]
    mov [rax + 8], rbx
    mov rbx, [rsp + 32]
    mov [rax + 16], rbx
    add rsp, X64_FRAME_META_SIZE
    POP_GPRS
    iretq
%endmacro

%macro ISR_ERR 2
%1:
    PUSH_GPRS
    lea rax, [rsp + X64_FRAME_GPR_SIZE]
    push qword 0
    lea rbx, [rax + 32]
    push rbx
    push qword [rax + 24]
    push qword [rax + 16]
    push qword [rax + 8]
    push qword [rax + 0]
    push qword %2
    CALL_DISPATCH
    lea rax, [rsp + X64_FRAME_META_SIZE + X64_FRAME_GPR_SIZE]
    mov rbx, [rsp + 16]
    mov [rax + 8], rbx
    mov rbx, [rsp + 24]
    mov [rax + 16], rbx
    mov rbx, [rsp + 32]
    mov [rax + 24], rbx
    add rsp, X64_FRAME_META_SIZE
    POP_GPRS
    add rsp, 8
    iretq
%endmacro

SECTION .text

ISR_NOERR x64_isr_default, 255
ISR_NOERR x64_isr_invalid_opcode, 6
ISR_ERR   x64_isr_double_fault, 8
ISR_ERR   x64_isr_gpf, 13
ISR_ERR   x64_isr_page_fault, 14
ISR_NOERR x64_irq_timer, 32
ISR_NOERR x64_irq_keyboard, 33
ISR_NOERR x64_irq_mouse, 44

x64_run_user_task:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rel user_kernel_resume_esp], rsp

    mov r11, [rel user_current_task_frame_ptr]
    test r11, r11
    jz .return_kernel

    mov ax, [r11 + TF_USER_SS]
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push qword [r11 + TF_USER_SS]
    push qword [r11 + TF_USER_RSP]
    push qword [r11 + TF_RFLAGS]
    push qword [r11 + TF_CS]
    push qword [r11 + TF_RIP]

    mov r15, [r11 + TF_R15]
    mov r14, [r11 + TF_R14]
    mov r13, [r11 + TF_R13]
    mov r12, [r11 + TF_R12]
    mov r9, [r11 + TF_R9]
    mov r8, [r11 + TF_R8]
    mov rbp, [r11 + TF_RBP]
    mov rdi, [r11 + TF_RDI]
    mov rsi, [r11 + TF_RSI]
    mov rdx, [r11 + TF_RDX]
    mov rcx, [r11 + TF_RCX]
    mov rbx, [r11 + TF_RBX]
    mov rax, [r11 + TF_RAX]
    mov r10, [r11 + TF_R10]
    mov r11, [r11 + TF_R11]
    iretq

.return_kernel:
    mov rsp, [rel user_kernel_resume_esp]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

x64_isr_syscall:
    PUSH_GPRS

    lea rax, [rsp + X64_FRAME_GPR_SIZE]
    push qword [rax + 32]
    push qword [rax + 24]
    push qword [rax + 16]
    push qword [rax + 8]
    push qword [rax + 0]
    push qword 0
    push qword 0x80

    mov ax, X64_GDT_KERNEL_DATA
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov rdi, rsp
    mov rax, rsp
    and rax, 0xF
    jz .syscall_aligned
    sub rsp, 8
    call syscall_handler
    add rsp, 8
    jmp .syscall_done
.syscall_aligned:
    call syscall_handler
.syscall_done:

    cmp qword [rel user_kernel_return_mode], 0
    jne .syscall_return_kernel

    lea rax, [rsp + X64_FRAME_META_SIZE + X64_FRAME_GPR_SIZE]
    mov rbx, [rsp + TF_RIP]
    mov [rax + 0], rbx
    mov rbx, [rsp + TF_CS]
    mov [rax + 8], rbx
    mov rbx, [rsp + TF_RFLAGS]
    mov [rax + 16], rbx
    mov rbx, [rsp + TF_USER_RSP]
    mov [rax + 24], rbx
    mov rbx, [rsp + TF_USER_SS]
    mov [rax + 32], rbx

    mov ax, [rsp + TF_USER_SS]
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    add rsp, X64_FRAME_META_SIZE
    POP_GPRS
    iretq

.syscall_return_kernel:
    mov qword [rel user_kernel_return_mode], 0
    mov rsp, [rel user_kernel_resume_esp]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

x64_isr_user_yield:
    PUSH_GPRS

    lea rax, [rsp + X64_FRAME_GPR_SIZE]
    push qword [rax + 32]
    push qword [rax + 24]
    push qword [rax + 16]
    push qword [rax + 8]
    push qword [rax + 0]
    push qword 0
    push qword 0x81

    mov ax, X64_GDT_KERNEL_DATA
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov rdi, rsp
    mov rax, rsp
    and rax, 0xF
    jz .yield_aligned
    sub rsp, 8
    call user_yield_handler
    add rsp, 8
    jmp .yield_done
.yield_aligned:
    call user_yield_handler
.yield_done:

    mov rsp, [rel user_kernel_resume_esp]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

x64_test_invalid_opcode:
    ud2
x64_test_invalid_opcode_resume:
    ret

x64_test_page_fault:
    push rbx
    mov rax, 0xFFFF800010000000
    mov rbx, 0x1122334455667788
    mov qword [rax], rbx
x64_test_page_fault_resume:
    pop rbx
    ret

x64_test_gpf:
    mov ax, 0x20
    mov ds, ax
x64_test_gpf_resume:
    ret
