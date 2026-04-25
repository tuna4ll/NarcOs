#ifndef ARCH_H
#define ARCH_H

#include <stdint.h>

typedef struct {
    uint64_t vector;
    uint64_t error_code;
    union {
        uint64_t rip;
        uint32_t eip;
    };
    uint64_t cs;
    uint64_t rflags;
    union {
        uint64_t user_rsp;
        uint32_t user_esp;
    };
    uint64_t user_ss;
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    union {
        uint64_t rbp;
        uint32_t ebp;
    };
    union {
        uint64_t rdi;
        uint32_t edi;
    };
    union {
        uint64_t rsi;
        uint32_t esi;
    };
    union {
        uint64_t rdx;
        uint32_t edx;
    };
    union {
        uint64_t rcx;
        uint32_t ecx;
    };
    union {
        uint64_t rbx;
        uint32_t ebx;
    };
    union {
        uint64_t rax;
        uint32_t eax;
    };
} arch_trap_frame_t;

typedef struct {
    uintptr_t kernel_sp;
    void* kernel_stack_base;
    uint32_t kernel_stack_pages;
    uintptr_t kernel_stack_top;
    void* user_trap_stack_base;
    uint32_t user_trap_stack_pages;
    uintptr_t user_trap_stack_top;
    uintptr_t address_space_root;
    arch_trap_frame_t user_frame;
} arch_process_state_t;

typedef struct {
    uint64_t number;
    uint64_t arg0;
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
} arch_syscall_state_t;

void arch_init_cpu(void);
void arch_init_legacy_pic(void);
void arch_init_timer(uint32_t hz);
void arch_init_interrupts(void);
void arch_init_memory(void);
uintptr_t arch_read_fault_address(void);

void arch_set_kernel_stack(uintptr_t stack_top);
void arch_switch_task(uintptr_t* old_sp, uintptr_t new_sp);
void arch_enter_user(arch_trap_frame_t* frame);

void arch_user_frame_init(arch_trap_frame_t* frame, uintptr_t entry_point, uintptr_t user_stack_top);
void arch_user_frame_set_exec_class(arch_trap_frame_t* frame, uint8_t image_class);
void arch_user_frame_set_task_arg(arch_trap_frame_t* frame, uintptr_t arg);
void arch_user_frame_set_exec_start(arch_trap_frame_t* frame, uintptr_t argc, uintptr_t argv, uintptr_t user_stack_top);
void arch_user_frame_sanitize(arch_trap_frame_t* frame);

void arch_syscall_capture(const arch_trap_frame_t* frame, arch_syscall_state_t* state);

static inline uintptr_t arch_frame_user_ip(const arch_trap_frame_t* frame) {
    return frame ? (uintptr_t)frame->rip : 0U;
}

static inline uintptr_t arch_frame_user_sp(const arch_trap_frame_t* frame) {
    return frame ? (uintptr_t)frame->user_rsp : 0U;
}

static inline uintptr_t arch_frame_return_value(const arch_trap_frame_t* frame) {
    return frame ? (uintptr_t)frame->rax : 0U;
}

static inline void arch_frame_set_return_value(arch_trap_frame_t* frame, uintptr_t value) {
    if (!frame) return;
    frame->rax = (uint64_t)value;
}

#endif
