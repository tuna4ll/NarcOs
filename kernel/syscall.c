#include "syscall.h"
#include <stdint.h>
#include "vbe.h"
#include "memory_alloc.h"
#include "fs.h"
#include "usermode.h"

extern void vga_println(const char* str);
extern void vga_print_color(const char* str, uint8_t color);
extern void vbe_compose_scene_basic();
extern int get_mouse_x();
extern int get_mouse_y();
static uint32_t last_gui_tick = 0;
extern uint32_t timer_ticks;
static uint32_t kernel_rng_state = 0xA341316Cu;

static uint32_t kernel_next_random() {
    uint32_t mix = timer_ticks ^ ((uint32_t)get_mouse_x() << 16) ^ (uint32_t)get_mouse_y();
    kernel_rng_state ^= mix + 0x9E3779B9u;
    kernel_rng_state ^= kernel_rng_state << 13;
    kernel_rng_state ^= kernel_rng_state >> 17;
    kernel_rng_state ^= kernel_rng_state << 5;
    if (kernel_rng_state == 0) kernel_rng_state = 0x6D2B79F5u;
    return kernel_rng_state;
}

void syscall_handler(trap_frame_t* frame) {
    uint32_t eax = frame->eax;
    uint32_t ebx = frame->ebx;
    uint32_t ecx = frame->ecx;
    uint32_t edx = frame->edx;

    if (eax == SYS_PRINT) {
        vga_println((const char*)ebx);
    } else if (eax == SYS_MALLOC) {
        frame->eax = (uint32_t)malloc((size_t)ebx);
    } else if (eax == SYS_FREE) {
        free((void*)ebx);
    } else if (eax == SYS_GUI_UPDATE) {
        if (timer_ticks - last_gui_tick > 2) { 
            vbe_compose_scene_basic();
            vbe_update();
            last_gui_tick = timer_ticks;
        }
        
        volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
        static int hb = 0;
        vga[78] = (hb++ % 2) ? 0x2F21 : 0x2F20; // '!' blinking in green
    } else if (eax == SYS_YIELD) {
        frame->eax = 0;
    } else if (eax == SYS_UPTIME) {
        frame->eax = timer_ticks;
    } else if (eax == SYS_GETPID) {
        frame->eax = 1;
    } else if (eax == SYS_CHDIR) {
        frame->eax = (uint32_t)fs_change_dir((const char*)ebx);
    } else if (eax == SYS_FS_READ) {
        frame->eax = (uint32_t)fs_read_file((const char*)ebx, (char*)ecx, (size_t)edx);
    } else if (eax == SYS_FS_WRITE) {
        frame->eax = (uint32_t)fs_write_file((const char*)ebx, (const char*)ecx);
    } else if (eax == SYS_SNAKE_GET_INPUT) {
        frame->eax = (uint32_t)consume_user_snake_input();
    } else if (eax == SYS_SNAKE_CLOSE) {
        stop_user_snake();
        frame->eax = 0;
    } else if (eax == SYS_RANDOM) {
        frame->eax = kernel_next_random();
    } else if (eax == SYS_EXIT) {
        stop_user_snake();
        frame->eax = 0;
    }
}

extern void set_idt_gate(int n, uint32_t handler, uint8_t attributes);
extern void isr_syscall();
extern void isr_user_yield();

void init_syscalls() {
    set_idt_gate(0x80, (uint32_t)isr_syscall, 0xEF);
    set_idt_gate(0x81, (uint32_t)isr_user_yield, 0xEF);
}
