#include "arch.h"

#include "cpu.h"
#include "gdt.h"
#include "interrupts.h"
#include "x64_paging.h"

extern void x64_process_switch(uintptr_t* old_sp, uintptr_t new_sp);
extern void x64_run_user_task(void);

void arch_init_cpu(void) {
    x64_cpu_init();
}

void arch_init_legacy_pic(void) {
    x64_pic_init();
}

void arch_init_timer(uint32_t hz) {
    x64_pit_init(hz);
}

void arch_init_interrupts(void) {
    x64_gdt_init();
    x64_interrupt_init();
}

void arch_init_memory(void) {
    (void)x64_paging_init();
}

uintptr_t arch_read_fault_address(void) {
    return (uintptr_t)x64_read_cr2();
}

void arch_set_kernel_stack(uintptr_t stack_top) {
    x64_gdt_set_kernel_stack((uint64_t)stack_top);
}

void arch_switch_task(uintptr_t* old_sp, uintptr_t new_sp) {
    x64_process_switch(old_sp, new_sp);
}

void arch_enter_user(arch_trap_frame_t* frame) {
    (void)frame;
    x64_run_user_task();
}

void arch_user_frame_init(arch_trap_frame_t* frame, uintptr_t entry_point, uintptr_t user_stack_top) {
    if (!frame) return;

    frame->vector = 0;
    frame->error_code = 0;
    frame->rip = (uint64_t)entry_point;
    frame->cs = (uint64_t)(X64_GDT_USER_CODE64 | 0x3U);
    frame->rflags = 0x202U;
    frame->user_rsp = (uint64_t)user_stack_top;
    frame->user_ss = (uint64_t)(X64_GDT_USER_DATA | 0x3U);
    frame->r15 = 0;
    frame->r14 = 0;
    frame->r13 = 0;
    frame->r12 = 0;
    frame->r11 = 0;
    frame->r10 = 0;
    frame->r9 = 0;
    frame->r8 = 0;
    frame->rbp = 0;
    frame->rdi = 0;
    frame->rsi = 0;
    frame->rdx = 0;
    frame->rcx = 0;
    frame->rbx = 0;
    frame->rax = 0;
}

void arch_user_frame_set_exec_class(arch_trap_frame_t* frame, uint8_t image_class) {
    (void)image_class;
    if (!frame) return;
    frame->cs = (uint64_t)(X64_GDT_USER_CODE64 | 0x3U);
    frame->user_ss = (uint64_t)(X64_GDT_USER_DATA | 0x3U);
}

void arch_user_frame_set_task_arg(arch_trap_frame_t* frame, uintptr_t arg) {
    if (!frame) return;
    frame->rdi = (uint64_t)arg;
}

void arch_user_frame_set_exec_start(arch_trap_frame_t* frame, uintptr_t argc, uintptr_t argv, uintptr_t user_stack_top) {
    if (!frame) return;
    frame->rdi = (uint64_t)argc;
    frame->rsi = (uint64_t)argv;
    frame->rax = 0;
    frame->rbx = 0;
    frame->user_rsp = (uint64_t)user_stack_top;
}

void arch_user_frame_sanitize(arch_trap_frame_t* frame) {
    if (!frame) return;
    frame->cs = (uint64_t)(X64_GDT_USER_CODE64 | 0x3U);
    frame->user_ss = (uint64_t)(X64_GDT_USER_DATA | 0x3U);
    frame->rflags |= 0x200U;
}

void arch_syscall_capture(const arch_trap_frame_t* frame, arch_syscall_state_t* state) {
    if (!frame || !state) return;
    state->number = frame->rax;
    state->arg0 = frame->rbx;
    state->arg1 = frame->rcx;
    state->arg2 = frame->rdx;
    state->arg3 = frame->rsi;
}
