#include "syscall.h"
#include <stdint.h>
#include "vbe.h"

extern void vga_println(const char* str);
extern void vga_print_color(const char* str, uint8_t color);
extern void vbe_compose_scene_basic();
extern int get_mouse_x();
extern int get_mouse_y();

static void* syscalls[] = {
    0, // SYS_EXIT
    (void*)vga_println,
    0, // SYS_MALLOC
    0, // SYS_FREE
    (void*)vbe_update
};

uint32_t num_syscalls = 5;
static uint32_t last_gui_tick = 0;
extern uint32_t timer_ticks;

void syscall_handler(trap_frame_t* frame) {
    uint32_t eax = frame->eax;
    uint32_t ebx = frame->ebx;

    if (eax >= num_syscalls) return;

    void* location = syscalls[eax];
    if (!location) return;

    if (eax == SYS_PRINT) {
        typedef void (*print_fn)(const char*);
        ((print_fn)location)((const char*)ebx);
    } else if (eax == SYS_GUI_UPDATE) {
        if (timer_ticks - last_gui_tick > 2) { 
            vbe_compose_scene_basic();
            vbe_update();
            last_gui_tick = timer_ticks;
        }
        
        volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
        static int hb = 0;
        vga[78] = (hb++ % 2) ? 0x2F21 : 0x2F20; // '!' blinking in green
    }
}

extern void set_idt_gate(int n, uint32_t handler, uint8_t attributes);
extern void isr_syscall();

void init_syscalls() {
    set_idt_gate(0x80, (uint32_t)isr_syscall, 0xEE);
}
