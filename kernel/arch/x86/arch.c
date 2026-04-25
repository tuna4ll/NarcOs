#include "arch.h"
#include "cpu.h"
#include "gdt.h"
#include "io.h"
#include "paging.h"
#include "serial.h"
#include "string.h"

static idt_entry_t idt[256];
static idtr_t idtr;

extern void irq0_timer(void);
extern void irq1_keyboard(void);
extern void irq12_mouse(void);
extern void isr_default(void);
extern void isr_double_fault(void);
extern void isr_gpf(void);
extern void isr_invalid_opcode(void);
extern void isr_page_fault(void);
extern void isr_stack_fault(void);
extern void isr_syscall(void);
extern void isr_user_yield(void);
extern void process_switch(uint32_t* old_esp, uint32_t new_esp);
extern void run_user_task(arch_trap_frame_t* frame);

static void arch_set_idt_gate(int index, uint32_t handler, uint8_t attributes) {
    idt[index].isr_low = (uint16_t)(handler & 0xFFFFU);
    idt[index].kernel_cs = KERNEL_CODE_SEG;
    idt[index].reserved = 0;
    idt[index].attributes = attributes;
    idt[index].isr_high = (uint16_t)((handler >> 16) & 0xFFFFU);
}

static void arch_init_idt_core(void) {
    idtr.base = (uint32_t)&idt;
    idtr.limit = (uint16_t)(sizeof(idt) - 1U);

    for (int i = 0; i < 256; i++) {
        arch_set_idt_gate(i, (uint32_t)isr_default, 0x8E);
    }

    arch_set_idt_gate(6, (uint32_t)isr_invalid_opcode, 0x8E);
    arch_set_idt_gate(8, (uint32_t)isr_double_fault, 0x8E);
    arch_set_idt_gate(12, (uint32_t)isr_stack_fault, 0x8E);
    arch_set_idt_gate(13, (uint32_t)isr_gpf, 0x8E);
    arch_set_idt_gate(14, (uint32_t)isr_page_fault, 0x8E);
    arch_set_idt_gate(32, (uint32_t)irq0_timer, 0x8E);
    arch_set_idt_gate(33, (uint32_t)irq1_keyboard, 0x8E);
    arch_set_idt_gate(44, (uint32_t)irq12_mouse, 0x8E);
}

static void arch_install_syscall_gates(void) {
    arch_set_idt_gate(0x80, (uint32_t)isr_syscall, 0xEF);
    arch_set_idt_gate(0x81, (uint32_t)isr_user_yield, 0xEF);
}

void arch_init_cpu(void) {
    cpu_init();
}

void arch_init_legacy_pic(void) {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    outb(0x21, 0xF8);
    outb(0xA1, 0xEF);
}

void arch_init_timer(uint32_t hz) {
    uint32_t divisor;

    if (hz == 0U) return;
    divisor = 1193180U / hz;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFFU));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFFU));
}

void arch_init_interrupts(void) {
    serial_write_line("[boot] init_gdt");
    init_gdt();
    serial_write_line("[boot] load_idt");
    arch_init_idt_core();
    serial_write_line("[boot] init_syscalls");
    arch_install_syscall_gates();
    asm volatile("lidt %0" : : "m"(idtr));
    asm volatile("sti");
}

void arch_init_memory(void) {
    init_paging();
}

uint32_t arch_read_fault_address(void) {
    uint32_t value;

    asm volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

void arch_set_kernel_stack(uint32_t stack_top) {
    set_tss_stack(stack_top);
}

void arch_switch_task(uint32_t* old_sp, uint32_t new_sp) {
    process_switch(old_sp, new_sp);
}

void arch_enter_user(arch_trap_frame_t* frame) {
    run_user_task(frame);
}

void arch_user_frame_init(arch_trap_frame_t* frame, uint32_t entry_point, uint32_t user_stack_top) {
    if (!frame) return;

    memset(frame, 0, sizeof(*frame));
    frame->gs = USER_DATA_SEG;
    frame->fs = USER_DATA_SEG;
    frame->es = USER_DATA_SEG;
    frame->ds = USER_DATA_SEG;
    frame->eip = entry_point;
    frame->cs = USER_CODE_SEG;
    frame->eflags = 0x202U;
    frame->user_esp = user_stack_top;
    frame->user_ss = USER_DATA_SEG;
}

void arch_user_frame_set_exec_class(arch_trap_frame_t* frame, uint8_t image_class) {
    (void)frame;
    (void)image_class;
}

void arch_user_frame_set_task_arg(arch_trap_frame_t* frame, uint32_t arg) {
    if (!frame) return;
    frame->edi = arg;
}

void arch_user_frame_set_exec_start(arch_trap_frame_t* frame, uint32_t argc, uint32_t argv, uint32_t user_stack_top) {
    if (!frame) return;
    frame->eax = argc;
    frame->ebx = argv;
    frame->user_esp = user_stack_top;
}

void arch_user_frame_sanitize(arch_trap_frame_t* frame) {
    if (!frame) return;
    frame->gs = USER_DATA_SEG;
    frame->fs = USER_DATA_SEG;
    frame->es = USER_DATA_SEG;
    frame->ds = USER_DATA_SEG;
    frame->cs = USER_CODE_SEG;
    frame->user_ss = USER_DATA_SEG;
    frame->eflags |= 0x200U;
}

void arch_syscall_capture(const arch_trap_frame_t* frame, arch_syscall_state_t* state) {
    if (!frame || !state) return;
    state->number = frame->eax;
    state->arg0 = frame->ebx;
    state->arg1 = frame->ecx;
    state->arg2 = frame->edx;
    state->arg3 = frame->esi;
}
