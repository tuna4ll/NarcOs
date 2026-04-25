#ifndef ARCH_H
#define ARCH_H

#include <stdint.h>

typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_unused, ebx, edx, ecx, eax;
    uint32_t error_code;
    uint32_t eip, cs, eflags, user_esp, user_ss;
} arch_trap_frame_t;

typedef struct {
    uint32_t kernel_sp;
    void* kernel_stack_base;
    uint32_t kernel_stack_pages;
    uint32_t kernel_stack_top;
    void* user_trap_stack_base;
    uint32_t user_trap_stack_pages;
    uint32_t user_trap_stack_top;
    uint32_t address_space_root;
    arch_trap_frame_t user_frame;
} arch_process_state_t;

typedef struct {
    uint32_t number;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t arg2;
    uint32_t arg3;
} arch_syscall_state_t;

void arch_init_cpu(void);
void arch_init_legacy_pic(void);
void arch_init_timer(uint32_t hz);
void arch_init_interrupts(void);
void arch_init_memory(void);
uint32_t arch_read_fault_address(void);

void arch_set_kernel_stack(uint32_t stack_top);
void arch_switch_task(uint32_t* old_sp, uint32_t new_sp);
void arch_enter_user(arch_trap_frame_t* frame);

void arch_user_frame_init(arch_trap_frame_t* frame, uint32_t entry_point, uint32_t user_stack_top);
void arch_user_frame_set_exec_class(arch_trap_frame_t* frame, uint8_t image_class);
void arch_user_frame_set_task_arg(arch_trap_frame_t* frame, uint32_t arg);
void arch_user_frame_set_exec_start(arch_trap_frame_t* frame, uint32_t argc, uint32_t argv, uint32_t user_stack_top);
void arch_user_frame_sanitize(arch_trap_frame_t* frame);

void arch_syscall_capture(const arch_trap_frame_t* frame, arch_syscall_state_t* state);

static inline uintptr_t arch_frame_user_ip(const arch_trap_frame_t* frame) {
    return frame ? (uintptr_t)frame->eip : 0U;
}

static inline uintptr_t arch_frame_user_sp(const arch_trap_frame_t* frame) {
    return frame ? (uintptr_t)frame->user_esp : 0U;
}

static inline uintptr_t arch_frame_return_value(const arch_trap_frame_t* frame) {
    return frame ? (uintptr_t)frame->eax : 0U;
}

static inline void arch_frame_set_return_value(arch_trap_frame_t* frame, uintptr_t value) {
    if (!frame) return;
    frame->eax = (uint32_t)value;
}

#endif
