#include "interrupts.h"

#include "arch.h"
#include "cpu.h"
#include "gdt.h"
#include "process.h"
#include "x64_serial.h"

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed)) x64_idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) x64_idtr_t;

typedef struct {
    uint64_t vector;
    uint64_t resume_rip;
    uint64_t fault_addr;
    uint64_t error_code;
    uint64_t error_mask;
    int test_kind;
    int active;
    int passed;
} x64_expected_fault_t;

static x64_idt_entry_t idt[256];
static x64_idtr_t idtr;
static volatile uint64_t pit_ticks = 0;
static volatile uint64_t keyboard_count = 0;
static volatile uint64_t mouse_count = 0;
static volatile uint8_t keyboard_last_scancode = 0;
static volatile int timer_logged = 0;
static x64_expected_fault_t expected_fault;

extern volatile uint32_t timer_ticks;

extern void x64_isr_default(void);
extern void x64_isr_invalid_opcode(void);
extern void x64_isr_double_fault(void);
extern void x64_isr_gpf(void);
extern void x64_isr_page_fault(void);
extern void x64_irq_timer(void);
extern void x64_irq_keyboard(void);
extern void x64_irq_mouse(void);
extern void x64_isr_syscall(void);
extern void x64_isr_user_yield(void);
extern void handle_keyboard(void);
extern void handle_mouse(void);
extern void gpf_handler(arch_trap_frame_t* frame);
extern void page_fault_handler(arch_trap_frame_t* frame);
extern void invalid_opcode_handler(arch_trap_frame_t* frame);

static void x64_idt_set_gate(int vector, void (*handler)(void), uint8_t attributes, uint8_t ist) {
    uint64_t address = (uint64_t)(uintptr_t)handler;

    idt[vector].offset_low = (uint16_t)(address & 0xFFFFU);
    idt[vector].selector = X64_GDT_KERNEL_CODE;
    idt[vector].ist = (uint8_t)(ist & 0x7U);
    idt[vector].attributes = attributes;
    idt[vector].offset_mid = (uint16_t)((address >> 16) & 0xFFFFU);
    idt[vector].offset_high = (uint32_t)((address >> 32) & 0xFFFFFFFFU);
    idt[vector].reserved = 0;
}

static void x64_pic_eoi(uint64_t vector) {
    if (vector >= 40U && vector < 48U) x64_outb(0xA0, 0x20);
    if (vector >= 32U && vector < 48U) x64_outb(0x20, 0x20);
}

static void x64_log_frame(const char* tag, x64_trap_frame_t* frame, uint64_t extra_value, const char* extra_label) {
    x64_serial_write("[trap] ");
    x64_serial_write_line(tag);
    x64_serial_write("[trap] vector=");
    x64_serial_write_hex64(frame->vector);
    x64_serial_write(" error=");
    x64_serial_write_hex64(frame->error_code);
    if (extra_label) {
        x64_serial_write(" ");
        x64_serial_write(extra_label);
        x64_serial_write("=");
        x64_serial_write_hex64(extra_value);
    }
    x64_serial_write_char('\n');

    x64_serial_write("[trap] rip=");
    x64_serial_write_hex64(frame->rip);
    x64_serial_write(" cs=");
    x64_serial_write_hex64(frame->cs);
    x64_serial_write(" rflags=");
    x64_serial_write_hex64(frame->rflags);
    x64_serial_write(" rsp=");
    x64_serial_write_hex64(frame->rsp);
    x64_serial_write(" ss=");
    x64_serial_write_hex64(frame->ss);
    x64_serial_write_char('\n');

    x64_serial_write("[trap] rax=");
    x64_serial_write_hex64(frame->rax);
    x64_serial_write(" rbx=");
    x64_serial_write_hex64(frame->rbx);
    x64_serial_write(" rcx=");
    x64_serial_write_hex64(frame->rcx);
    x64_serial_write(" rdx=");
    x64_serial_write_hex64(frame->rdx);
    x64_serial_write_char('\n');

    x64_serial_write("[trap] rsi=");
    x64_serial_write_hex64(frame->rsi);
    x64_serial_write(" rdi=");
    x64_serial_write_hex64(frame->rdi);
    x64_serial_write(" rbp=");
    x64_serial_write_hex64(frame->rbp);
    x64_serial_write(" r8=");
    x64_serial_write_hex64(frame->r8);
    x64_serial_write_char('\n');

    x64_serial_write("[trap] r9=");
    x64_serial_write_hex64(frame->r9);
    x64_serial_write(" r10=");
    x64_serial_write_hex64(frame->r10);
    x64_serial_write(" r11=");
    x64_serial_write_hex64(frame->r11);
    x64_serial_write(" r12=");
    x64_serial_write_hex64(frame->r12);
    x64_serial_write_char('\n');

    x64_serial_write("[trap] r13=");
    x64_serial_write_hex64(frame->r13);
    x64_serial_write(" r14=");
    x64_serial_write_hex64(frame->r14);
    x64_serial_write(" r15=");
    x64_serial_write_hex64(frame->r15);
    x64_serial_write_char('\n');
}

static void x64_panic_halt(void) {
    process_debug_dump("x64-trap");
    x64_cli();
    for (;;) {
        x64_hlt();
    }
}

static int x64_try_expected_fault_recovery(x64_trap_frame_t* frame, uint64_t observed_fault_addr) {
    if (!expected_fault.active) return 0;
    if (expected_fault.vector != frame->vector) return 0;
    if (frame->vector == 14U && expected_fault.fault_addr != observed_fault_addr) return 0;
    if ((frame->error_code & expected_fault.error_mask) != (expected_fault.error_code & expected_fault.error_mask)) return 0;

    expected_fault.active = 0;
    expected_fault.passed = 1;
    frame->rip = expected_fault.resume_rip;
    x64_serial_write("[test] recovered from vector ");
    x64_serial_write_hex64(frame->vector);
    x64_serial_write_char('\n');
    return 1;
}

void x64_interrupt_init(void) {
    idtr.limit = (uint16_t)(sizeof(idt) - 1U);
    idtr.base = (uint64_t)(uintptr_t)&idt;

    for (int i = 0; i < 256; i++) {
        x64_idt_set_gate(i, x64_isr_default, 0x8EU, 0U);
    }

    x64_idt_set_gate(6, x64_isr_invalid_opcode, 0x8EU, 0U);
    x64_idt_set_gate(8, x64_isr_double_fault, 0x8EU, 1U);
    x64_idt_set_gate(13, x64_isr_gpf, 0x8EU, 0U);
    x64_idt_set_gate(14, x64_isr_page_fault, 0x8EU, 0U);
    x64_idt_set_gate(32, x64_irq_timer, 0x8EU, 0U);
    x64_idt_set_gate(33, x64_irq_keyboard, 0x8EU, 0U);
    x64_idt_set_gate(44, x64_irq_mouse, 0x8EU, 0U);
    x64_idt_set_gate(0x80, x64_isr_syscall, 0xEFU, 0U);
    x64_idt_set_gate(0x81, x64_isr_user_yield, 0xEFU, 0U);

    __asm__ volatile("lidt %0" : : "m"(idtr) : "memory");
}

void x64_pic_init(void) {
    x64_outb(0x20, 0x11);
    x64_outb(0xA0, 0x11);
    x64_outb(0x21, 0x20);
    x64_outb(0xA1, 0x28);
    x64_outb(0x21, 0x04);
    x64_outb(0xA1, 0x02);
    x64_outb(0x21, 0x01);
    x64_outb(0xA1, 0x01);
    x64_outb(0x21, 0xF8);
    x64_outb(0xA1, 0xEF);
}

void x64_pit_init(uint32_t hz) {
    uint32_t divisor;

    if (hz == 0U) return;
    divisor = 1193180U / hz;
    x64_outb(0x43, 0x36);
    x64_outb(0x40, (uint8_t)(divisor & 0xFFU));
    x64_outb(0x40, (uint8_t)((divisor >> 8) & 0xFFU));
}

uint64_t x64_timer_ticks(void) {
    return pit_ticks;
}

uint64_t x64_keyboard_irq_count(void) {
    return keyboard_count;
}

uint8_t x64_keyboard_last_byte(void) {
    return keyboard_last_scancode;
}

uint64_t x64_mouse_irq_count(void) {
    return mouse_count;
}

static int x64_i8042_wait_input_empty(void) {
    for (uint32_t i = 0; i < 0x100000U; i++) {
        if ((x64_inb(0x64) & 0x02U) == 0U) return 0;
    }
    return -1;
}

static void x64_i8042_drain_output(void) {
    for (uint32_t i = 0; i < 32U; i++) {
        if ((x64_inb(0x64) & 0x01U) == 0U) break;
        (void)x64_inb(0x60);
    }
}

int x64_keyboard_send_echo(void) {
    x64_i8042_drain_output();
    if (x64_i8042_wait_input_empty() != 0) return -1;
    x64_outb(0x60, 0xEE);
    return 0;
}

void x64_expect_fault(uint64_t vector, uint64_t resume_rip, uint64_t fault_addr, int test_kind) {
    x64_expect_fault_with_error(vector, resume_rip, fault_addr, 0, 0, test_kind);
}

void x64_expect_fault_with_error(uint64_t vector, uint64_t resume_rip, uint64_t fault_addr,
                                 uint64_t error_code, uint64_t error_mask, int test_kind) {
    expected_fault.vector = vector;
    expected_fault.resume_rip = resume_rip;
    expected_fault.fault_addr = fault_addr;
    expected_fault.error_code = error_code;
    expected_fault.error_mask = error_mask;
    expected_fault.test_kind = test_kind;
    expected_fault.active = 1;
    expected_fault.passed = 0;
}

int x64_last_fault_test_passed(void) {
    return expected_fault.passed;
}

void x64_interrupt_dispatch(x64_trap_frame_t* frame) {
    uint64_t fault_addr = 0;

    if (!frame) x64_panic_halt();

    switch (frame->vector) {
        case 32:
            pit_ticks++;
            timer_ticks++;
            process_on_timer_tick();
            if (!timer_logged) {
                timer_logged = 1;
                x64_serial_write("[irq] PIT tick=");
                x64_serial_write_hex64(pit_ticks);
                x64_serial_write_char('\n');
            }
            x64_pic_eoi(frame->vector);
            return;

        case 33:
            keyboard_count++;
            handle_keyboard();
            keyboard_last_scancode = 0;
            return;

        case 44:
            mouse_count++;
            handle_mouse();
            return;

        case 0x80:
            x64_log_frame("user syscall stub", frame, 0, 0);
            break;

        case 0x81:
            x64_log_frame("user yield stub", frame, 0, 0);
            break;

        case 6:
            x64_log_frame("invalid opcode", frame, 0, 0);
            if (x64_try_expected_fault_recovery(frame, 0)) return;
            invalid_opcode_handler((arch_trap_frame_t*)frame);
            return;

        case 8:
            x64_log_frame("double fault", frame, 0, 0);
            break;

        case 13:
            x64_log_frame("general protection fault", frame, 0, 0);
            if (x64_try_expected_fault_recovery(frame, 0)) return;
            gpf_handler((arch_trap_frame_t*)frame);
            return;

        case 14:
            fault_addr = x64_read_cr2();
            x64_log_frame("page fault", frame, fault_addr, "cr2");
            if (x64_try_expected_fault_recovery(frame, fault_addr)) return;
            page_fault_handler((arch_trap_frame_t*)frame);
            return;

        default:
            x64_log_frame("unexpected vector", frame, 0, 0);
            if (frame->vector >= 32U && frame->vector < 48U) {
                x64_pic_eoi(frame->vector);
                return;
            }
            break;
    }

    x64_serial_write_line("[panic] halting after unrecovered trap");
    x64_panic_halt();
}
