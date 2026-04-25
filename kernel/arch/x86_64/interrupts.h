#ifndef NARCOS_X86_64_INTERRUPTS_H
#define NARCOS_X86_64_INTERRUPTS_H

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
        uint64_t rsp;
        uint32_t esp;
    };
    uint64_t ss;
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
} x64_trap_frame_t;

enum {
    X64_TEST_NONE = 0,
    X64_TEST_INVALID_OPCODE = 1,
    X64_TEST_PAGE_FAULT = 2,
    X64_TEST_GPF = 3,
};

void x64_interrupt_init(void);
void x64_pic_init(void);
void x64_pit_init(uint32_t hz);
void x64_interrupt_dispatch(x64_trap_frame_t* frame);

uint64_t x64_timer_ticks(void);
uint64_t x64_keyboard_irq_count(void);
uint8_t x64_keyboard_last_byte(void);
uint64_t x64_mouse_irq_count(void);

int x64_keyboard_send_echo(void);

void x64_expect_fault(uint64_t vector, uint64_t resume_rip, uint64_t fault_addr, int test_kind);
void x64_expect_fault_with_error(uint64_t vector, uint64_t resume_rip, uint64_t fault_addr,
                                 uint64_t error_code, uint64_t error_mask, int test_kind);
int x64_last_fault_test_passed(void);

#endif
