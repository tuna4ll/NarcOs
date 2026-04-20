#include <stdint.h>
#include "string.h"
#include "fs.h"
#include "rtc.h"
#include "editor.h"
#include "memory_alloc.h"
#include "paging.h"
#include "process.h"
#include "fd.h"
#include "cpu.h"
#include "pci.h"
#include "storage.h"
#include "vbe.h"
#include "mouse.h"
#include "gdt.h"
#include "serial.h"
#include "syscall.h"
#include "usermode.h"
#include "net.h"

extern void outb(uint16_t port, uint8_t val);
extern void outw(uint16_t port, uint16_t val);
extern uint8_t inb(uint16_t port);
extern void clear_screen();
extern void screen_set_graphics_enabled(int enabled);
extern int screen_is_graphics_enabled(void);
extern void vga_print(const char* str);
extern void vga_print_color(const char* str, uint8_t color);
extern void vga_println(const char* str);
extern void vga_print_int(int num);
extern void init_keyboard();
extern disk_fs_node_t dir_cache[MAX_FILES];

extern void print_memory_info();

void vga_print_int_hex(uint32_t n, char* buf);

// Global variables for usermode jump
volatile uint32_t usermode_jump_eip;
volatile uint32_t usermode_jump_esp;

idt_entry_t idt[256];
idtr_t idtr;

volatile uint32_t timer_ticks = 0;
static volatile int kernel_graphics_ready = 0;
static volatile int kernel_waitpid_test_release = 0;

typedef struct {
    volatile int writer_status;
    volatile int reader_status;
    volatile uint32_t bytes_written;
    volatile uint32_t bytes_read;
    uint32_t target_bytes;
    int read_fd;
    int write_fd;
} kernel_pipe_test_state_t;

static kernel_pipe_test_state_t kernel_pipe_test_state;

window_t windows[MAX_WINDOWS];
int window_count = 0;
int active_window_idx = -1;

char pad_title[32] = "NarcPad";
volatile int snk_next_dir = -1;
int exp_drag_idx = -1;
int exp_drag_source_dir = -1;
int exp_drag_armed = 0;

int snk_px[100], snk_py[100], snk_len = 5, apple_x = 10, apple_y = 10;
int snk_dead = 0, snk_score = 0, snk_best = 0;

void nwm_init_windows() {
    windows[0].type = WIN_TYPE_TERMINAL;
    windows[0].x = 50; windows[0].y = 50;
    windows[0].w = 700; windows[0].h = 475;
    strcpy(windows[0].title, "Terminal");
    windows[0].visible = 0;
    windows[0].minimized = 0;
    windows[0].id = 0;
    windows[1].type = WIN_TYPE_EXPLORER;
    windows[1].x = 150; windows[1].y = 100;
    windows[1].w = 760; windows[1].h = 430;
    strcpy(windows[1].title, "Explorer");
    windows[1].visible = 0;
    windows[1].minimized = 0;
    windows[1].id = 1;
    windows[2].type = WIN_TYPE_NARCPAD;
    windows[2].x = 200; windows[2].y = 150;
    windows[2].w = 500; windows[2].h = 400;
    strcpy(windows[2].title, "Text Editor");
    windows[2].visible = 0;
    windows[2].minimized = 0;
    windows[2].id = 2;
    windows[3].type = WIN_TYPE_SNAKE;
    windows[3].x = 300; windows[3].y = 200;
    windows[3].w = 400; windows[3].h = 372;
    strcpy(windows[3].title, "Snake");
    windows[3].visible = 0;
    windows[3].minimized = 0;
    windows[3].id = 3;
    windows[4].type = WIN_TYPE_SETTINGS;
    windows[4].x = 220; windows[4].y = 110;
    windows[4].w = 520; windows[4].h = 330;
    strcpy(windows[4].title, "Settings");
    windows[4].visible = 0;
    windows[4].minimized = 0;
    windows[4].id = 4;

    window_count = 5;
}

void nwm_bring_to_front(int idx) {
    if (idx < 0 || idx >= window_count) return;
    window_t tmp = windows[idx];
    for (int i = idx; i < window_count - 1; i++) {
        windows[i] = windows[i+1];
    }
    windows[window_count - 1] = tmp;
    active_window_idx = window_count - 1;
    gui_needs_redraw = 1;
}

int nwm_get_idx_by_type(window_type_t type) {
    for (int i = 0; i < window_count; i++) {
        if (windows[i].type == type) return i;
    }
    return -1;
}

int nwm_find_window_at(int mx, int my) {
    for (int i = window_count - 1; i >= 0; i--) {
        if (!windows[i].visible || windows[i].minimized) continue;
        if (mx >= windows[i].x && mx <= windows[i].x + windows[i].w &&
            my >= windows[i].y && my <= windows[i].y + windows[i].h) {
            return i;
        }
    }
    return -1;
}

#define DESKTOP_CLICK_TARGET_NONE           (-1)
#define DESKTOP_CLICK_TARGET_EXPLORER_ICON  1
#define DESKTOP_CLICK_TARGET_SNAKE_ICON     2
#define DESKTOP_CLICK_TARGET_FILE_BASE      100
#define DESKTOP_CLICK_TARGET_EXPLORER_BASE  1000
#define DESKTOP_DOUBLE_CLICK_TICKS          70U

static int get_desktop_click_target(int mx, int my) {
    if (mx < 20 || mx > 60) return DESKTOP_CLICK_TARGET_NONE;
    if (my >= 60 && my <= 110) return DESKTOP_CLICK_TARGET_EXPLORER_ICON;
    if (my >= 300 && my <= 350) return DESKTOP_CLICK_TARGET_SNAKE_ICON;
    if (my >= 140) return DESKTOP_CLICK_TARGET_FILE_BASE + ((my - 140) / 80);
    return DESKTOP_CLICK_TARGET_NONE;
}

static int get_explorer_list_click_target(int mx, int my, window_t* win) {
    int client_x;
    int client_y;
    int client_w;
    int sidebar_w;
    int content_x;
    int content_w;
    int panel_y;
    int list_y;
    int row_h;

    if (!win || win->type != WIN_TYPE_EXPLORER) return DESKTOP_CLICK_TARGET_NONE;
    client_x = win->x + 8;
    client_y = win->y + 40;
    client_w = win->w - 16;
    sidebar_w = 136;
    content_x = client_x + sidebar_w + 14;
    content_w = client_w - sidebar_w - 26;
    panel_y = client_y + 36;
    list_y = panel_y + 48;
    row_h = 54;

    if (mx < content_x + 16 || mx > content_x + content_w - 16 || my < list_y) {
        return DESKTOP_CLICK_TARGET_NONE;
    }
    return DESKTOP_CLICK_TARGET_EXPLORER_BASE + ((my - list_y) / row_h);
}

static void open_snake_window() {
    int idx = nwm_get_idx_by_type(WIN_TYPE_SNAKE);
    if (idx == -1) return;
    windows[idx].visible = 1;
    windows[idx].minimized = 0;
    nwm_bring_to_front(idx);
    if (!user_snake_running()) launch_user_snake();
    gui_needs_redraw = 1;
}

static void open_terminal_window() {
    int idx = nwm_get_idx_by_type(WIN_TYPE_TERMINAL);
    if (idx == -1) return;
    windows[idx].visible = 1;
    windows[idx].minimized = 0;
    nwm_bring_to_front(idx);
    gui_needs_redraw = 1;
}

static void open_explorer_window(int initial_dir) {
    int idx = nwm_get_idx_by_type(WIN_TYPE_EXPLORER);
    if (idx == -1) return;
    launch_user_explorer(initial_dir);
    queue_user_explorer_event(USER_EXPLORER_EVT_OPEN_DIR, initial_dir);
    windows[idx].visible = 1;
    windows[idx].minimized = 0;
    nwm_bring_to_front(idx);
    gui_needs_redraw = 1;
}

static void open_narcpad_window() {
    int idx = nwm_get_idx_by_type(WIN_TYPE_NARCPAD);
    if (idx == -1) return;
    launch_user_narcpad();
    request_user_narcpad_new();
    windows[idx].visible = 1;
    windows[idx].minimized = 0;
    nwm_bring_to_front(idx);
    gui_needs_redraw = 1;
}

static void open_settings_window() {
    int idx = nwm_get_idx_by_type(WIN_TYPE_SETTINGS);
    if (idx == -1) return;
    launch_user_settings();
    windows[idx].visible = 1;
    windows[idx].minimized = 0;
    nwm_bring_to_front(idx);
    gui_needs_redraw = 1;
}

static void open_file_in_narcpad_by_path(const char* path) {
    if (!path || path[0] == '\0') return;
    launch_user_narcpad();
    request_user_narcpad_open(path);
    {
        int pidx = nwm_get_idx_by_type(WIN_TYPE_NARCPAD);
        if (pidx == -1) return;
        windows[pidx].visible = 1;
        windows[pidx].minimized = 0;
        nwm_bring_to_front(pidx);
    }
    gui_needs_redraw = 1;
}

int kernel_gui_open_narcpad_file(const char* path) {
    if (!path || path[0] == '\0') return -1;
    open_file_in_narcpad_by_path(path);
    return 0;
}

static int settings_handle_click(window_t* win, int mx, int my) {
    int cx, cy, cw, ch;
    int local_x;
    int local_y;
    if (!win) return 0;
    vbe_get_window_client_rect(win, &cx, &cy, &cw, &ch);
    local_x = mx - cx;
    local_y = my - cy;
    if (local_x < 0 || local_y < 0 || local_x >= cw || local_y >= ch) return 0;
    if (!user_settings_running()) launch_user_settings();

    if (local_y >= 130 && local_y <= 152) {
        if (local_x >= 216 && local_x <= 278) {
            queue_user_settings_event(USER_SETTINGS_EVT_ADJUST_OFFSET, -30);
            return 1;
        }
        if (local_x >= 286 && local_x <= 348) {
            queue_user_settings_event(USER_SETTINGS_EVT_ADJUST_OFFSET, 30);
            return 1;
        }
        if (local_x >= 360 && local_x <= 486) {
            queue_user_settings_event(USER_SETTINGS_EVT_OPEN_CONFIG, 0);
            return 1;
        }
    }

    if (local_y >= 180 && local_y <= 202) {
        if (local_x >= 24 && local_x <= 92) {
            queue_user_settings_event(USER_SETTINGS_EVT_SET_OFFSET, -300);
            return 1;
        }
        if (local_x >= 102 && local_x <= 150) {
            queue_user_settings_event(USER_SETTINGS_EVT_SET_OFFSET, 0);
            return 1;
        }
        if (local_x >= 160 && local_x <= 228) {
            queue_user_settings_event(USER_SETTINGS_EVT_SET_OFFSET, 180);
            return 1;
        }
        if (local_x >= 238 && local_x <= 334) {
            queue_user_settings_event(USER_SETTINGS_EVT_SET_OFFSET, 330);
            return 1;
        }
        if (local_x >= 344 && local_x <= 412) {
            queue_user_settings_event(USER_SETTINGS_EVT_SET_OFFSET, 540);
            return 1;
        }
    }

    return 0;
}

static void explorer_open_dir(int new_dir) {
    if (!user_explorer_running()) launch_user_explorer(new_dir);
    queue_user_explorer_event(USER_EXPLORER_EVT_OPEN_DIR, new_dir);
}

int explorer_modal_active() { return user_explorer_running() && user_explorer_state.modal_mode != USER_EXPLORER_MODAL_NONE; }

void explorer_cancel_modal() {
    if (!user_explorer_running()) return;
    queue_user_explorer_event(USER_EXPLORER_EVT_MODAL_CANCEL, 0);
}

static int explorer_create_in_dir(int dir_idx, int is_dir) {
    (void)dir_idx;
    if (!user_explorer_running()) return -1;
    queue_user_explorer_event(is_dir ? USER_EXPLORER_EVT_CREATE_DIR : USER_EXPLORER_EVT_CREATE_FILE, 0);
    return 0;
}

static void explorer_open_selected(void) {
    if (!user_explorer_running()) return;
    queue_user_explorer_event(USER_EXPLORER_EVT_OPEN_SELECTED, 0);
}

static void explorer_open_with_selected(void) {
    explorer_open_selected();
}

static void explorer_begin_rename_selected(void) {
    if (!user_explorer_running()) return;
    queue_user_explorer_event(USER_EXPLORER_EVT_BEGIN_RENAME, 0);
}

static void explorer_begin_delete_selected(void) {
    if (!user_explorer_running()) return;
    queue_user_explorer_event(USER_EXPLORER_EVT_BEGIN_DELETE, 0);
}

void explorer_modal_append_char(char c) {
    if (!user_explorer_running()) return;
    queue_user_explorer_event(USER_EXPLORER_EVT_MODAL_CHAR, (int)c);
}

void explorer_modal_backspace() {
    if (!user_explorer_running()) return;
    queue_user_explorer_event(USER_EXPLORER_EVT_MODAL_BACKSPACE, 0);
}

void explorer_modal_submit() {
    if (!user_explorer_running()) return;
    queue_user_explorer_event(USER_EXPLORER_EVT_MODAL_SUBMIT, 0);
}

static int explorer_move_selected_to(int target_dir) {
    if (!user_explorer_running()) return -1;
    queue_user_explorer_event(USER_EXPLORER_EVT_MOVE_SELECTED_TO, target_dir);
    return 0;
}

extern void isr_default();
extern void irq0_timer();
extern void irq1_keyboard();
extern void irq12_mouse();
extern void isr_invalid_opcode();
extern void isr_stack_fault();
extern void isr_page_fault();

void set_idt_gate(int n, uint32_t handler, uint8_t attributes) {
    idt[n].isr_low = (uint16_t)(handler & 0xFFFF);
    idt[n].kernel_cs = 0x08;
    idt[n].reserved = 0;
    idt[n].attributes = attributes;
    idt[n].isr_high = (uint16_t)((handler >> 16) & 0xFFFF);
}

void init_pic() {
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

void init_pit() {
    uint32_t freq = 100; 
    uint32_t divisor = 1193180 / freq;
    outb(0x43, 0x36); 
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

extern void isr_gpf();
extern void isr_double_fault();

void load_idt() {
    idtr.base = (uint32_t)&idt;
    idtr.limit = 256 * sizeof(idt_entry_t) - 1;
    for (int i = 0; i < 256; i++) {
        set_idt_gate(i, (uint32_t)isr_default, 0x8E);
    }
    set_idt_gate(6, (uint32_t)isr_invalid_opcode, 0x8E);
    set_idt_gate(8, (uint32_t)isr_double_fault, 0x8E);
    set_idt_gate(12, (uint32_t)isr_stack_fault, 0x8E);
    set_idt_gate(13, (uint32_t)isr_gpf, 0x8E);
    set_idt_gate(14, (uint32_t)isr_page_fault, 0x8E);
    set_idt_gate(32, (uint32_t)irq0_timer, 0x8E);
    set_idt_gate(33, (uint32_t)irq1_keyboard, 0x8E);
    set_idt_gate(44, (uint32_t)irq12_mouse, 0x8E);
    asm volatile("lidt %0" : : "m"(idtr));
    asm volatile("sti");
}

void handle_timer() {
    timer_ticks++;
    process_on_timer_tick();
    
    // Kernel level heartbeat: SAFE VGA WRITE at (79, 0)
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    vga[79] = (timer_ticks % 20 < 10) ? 0x1F2A : 0x1F20; // Star blinking in blue

    outb(0x20, 0x20);
}

void isr_handler_default() {
    outb(0x20, 0x20);
    outb(0xA0, 0x20);
}

void vga_print_int_hex(uint32_t n, char* buf) {
    const char* hex = "0123456789ABCDEF";
    buf[0] = '0'; buf[1] = 'x';
    for(int i=0; i<8; i++) {
        buf[9-i] = hex[(n >> (i*4)) & 0x0F];
    }
    buf[10] = '\0';
}

static uint32_t read_cr2() {
    uint32_t value;
    asm volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

static void panic_serial_hex(const char* label, uint32_t value) {
    serial_write("[panic] ");
    serial_write(label);
    serial_write("=");
    serial_write_hex32(value);
    serial_write_char('\n');
}

static void panic_log_current_process() {
    process_t* current = process_current();

    if (!current) {
        serial_write_line("[panic] active pid=<none>");
        return;
    }

    serial_write("[panic] active pid=");
    serial_write_hex32((uint32_t)current->pid);
    serial_write(" ppid=");
    serial_write_hex32((uint32_t)current->parent_pid);
    serial_write(" kind=");
    serial_write(current->kind == PROCESS_KIND_USER ? "user" : "kernel");
    serial_write(" state=");
    if (current->state == PROC_RUNNABLE) serial_write("runnable");
    else if (current->state == PROC_RUNNING) serial_write("running");
    else if (current->state == PROC_ZOMBIE) serial_write("zombie");
    else serial_write("unused");
    serial_write(" name=");
    serial_write(current->name);
    if (current->image_path[0] != '\0') {
        serial_write(" image=");
        serial_write(current->image_path);
    }
    serial_write_char('\n');
}

static void panic_halt() {
    for (;;) {
        asm volatile("cli");
        asm volatile("hlt");
    }
}

static void boot_text_clear(uint8_t color) {
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    uint16_t cell = ((uint16_t)color << 8) | ' ';
    for (int i = 0; i < 80 * 25; i++) {
        vga[i] = cell;
    }
}

static void boot_text_write_line(int row, const char* text, uint8_t color) {
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    int col = 0;

    if (!text || row < 0 || row >= 25) return;
    while (text[col] != '\0' && col < 80) {
        vga[row * 80 + col] = ((uint16_t)color << 8) | (uint8_t)text[col];
        col++;
    }
}

static void boot_text_write_hex_line(int row, const char* label, uint32_t value, uint8_t color) {
    char hex_buf[16];
    char line[80];
    int off = 0;

    line[0] = '\0';
    if (label) {
        while (label[off] != '\0' && off < (int)sizeof(line) - 1) {
            line[off] = label[off];
            off++;
        }
        line[off] = '\0';
    }
    vga_print_int_hex(value, hex_buf);
    for (int i = 0; hex_buf[i] != '\0' && off < (int)sizeof(line) - 1; i++) {
        line[off++] = hex_buf[i];
    }
    line[off] = '\0';
    boot_text_write_line(row, line, color);
}

static void panic_text_exception(const char* title, const char* aux_label, uint32_t aux_value, trap_frame_t* frame) {
    boot_text_clear(0x1F);
    if (title) boot_text_write_line(0, title, 0x4F);
    if (aux_label) boot_text_write_hex_line(2, aux_label, aux_value, 0x1F);
    if (frame) {
        boot_text_write_hex_line(4, "Error: ", frame->error_code, 0x1F);
        boot_text_write_hex_line(5, "EIP:   ", frame->eip, 0x1F);
        boot_text_write_hex_line(6, "CS:    ", frame->cs, 0x1F);
        boot_text_write_hex_line(7, "ESP:   ", frame->user_esp, 0x1F);
        boot_text_write_hex_line(8, "SS:    ", frame->user_ss, 0x1F);
    }
    boot_text_write_line(10, "See serial log for more details.", 0x1E);
}

static int boot_framebuffer_available() {
    return *(uint32_t*)(0x6100 + 40) != 0 &&
           vbe_get_width() != 0 &&
           vbe_get_height() != 0 &&
           vbe_get_bpp() != 0;
}

static void boot_fatal(const char* headline, const char* detail) {
    serial_write_line("");
    serial_write_line("[boot] fatal");
    if (headline) serial_write_line(headline);
    if (detail) serial_write_line(detail);

    if (boot_framebuffer_available()) {
        init_vbe();
        kernel_graphics_ready = 1;
        vbe_clear(0x180000);
        vbe_draw_string(20, 20, "NarcOs boot failed", 0xFFFFFF);
        if (headline) vbe_draw_string(20, 54, headline, 0xFFB3B3);
        if (detail) vbe_draw_string(20, 78, detail, 0xFFE4A3);
        vbe_draw_string(20, 112, "See serial log for more details.", 0xFFFFFF);
        vbe_update();
    } else {
        boot_text_clear(0x1F);
        boot_text_write_line(0, "NarcOs boot failed", 0x1F);
        if (headline) boot_text_write_line(2, headline, 0x4F);
        if (detail) boot_text_write_line(4, detail, 0x1F);
        boot_text_write_line(6, "See serial log for more details.", 0x1E);
    }

    panic_halt();
}

static void paging_probe_kernel_vm() {
    volatile uint32_t* direct;
    uint32_t* mapped;
    void* phys_page = alloc_physical_page();

    if (!phys_page) {
        boot_fatal("Kernel VM probe failed.",
                   "Could not allocate a physical page for paging API validation.");
    }

    mapped = (uint32_t*)paging_map_physical((uint32_t)phys_page, 4096U, PAGING_FLAG_WRITE);
    if (!mapped) {
        free_physical_page(phys_page);
        boot_fatal("Kernel VM probe failed.",
                   "Dynamic virtual mapping window could not map a probe page.");
    }

    direct = (volatile uint32_t*)phys_page;
    mapped[0] = 0x4E415243U;
    mapped[1] = 0x4F532121U;
    if (direct[0] != 0x4E415243U || direct[1] != 0x4F532121U) {
        paging_unmap_virtual(mapped, 4096U);
        free_physical_page(phys_page);
        boot_fatal("Kernel VM probe failed.",
                   "Mapped writes were not visible through the identity mapping.");
    }

    paging_unmap_virtual(mapped, 4096U);
    free_physical_page(phys_page);
    serial_write("[boot] kernel_vm base=");
    serial_write_hex32(paging_kernel_vm_base());
    serial_write(" size=");
    serial_write_hex32(paging_kernel_vm_size());
    serial_write_char('\n');
}

static void panic_simple_exception(const char* tag, const char* title, uint32_t bg_color,
                                   const char* aux_label, uint32_t aux_value, trap_frame_t* frame) {
    char buf[64];

    serial_write_line("");
    serial_write("[panic] ");
    serial_write_line(tag);
    if (aux_label) panic_serial_hex(aux_label, aux_value);
    panic_serial_hex("error", frame->error_code);
    panic_serial_hex("eip", frame->eip);
    panic_serial_hex("cs", frame->cs);
    panic_serial_hex("user_esp", frame->user_esp);
    panic_serial_hex("user_ss", frame->user_ss);
    panic_log_current_process();

    if (!kernel_graphics_ready) {
        panic_text_exception(title, aux_label, aux_value, frame);
    } else {
        vbe_clear(bg_color);
        vbe_draw_string(20, 20, title, 0xFFFFFF);
        if (aux_label) {
            vbe_draw_string(20, 50, aux_label, 0xFFFFFF);
            vga_print_int_hex(aux_value, buf);
            vbe_draw_string(130, 50, buf, 0xFFD27F);
        }
        vbe_draw_string(20, 68, "Error:", 0xFFFFFF);
        vga_print_int_hex(frame->error_code, buf);
        vbe_draw_string(130, 68, buf, 0xFFD27F);
        vbe_draw_string(20, 86, "EIP:", 0xFFFFFF);
        vga_print_int_hex(frame->eip, buf);
        vbe_draw_string(130, 86, buf, 0xFFD27F);
        vbe_draw_string(20, 104, "CS:", 0xFFFFFF);
        vga_print_int_hex(frame->cs, buf);
        vbe_draw_string(130, 104, buf, 0xFFD27F);
        vbe_draw_string(20, 122, "USER ESP:", 0xFFFFFF);
        vga_print_int_hex(frame->user_esp, buf);
        vbe_draw_string(130, 122, buf, 0xFFD27F);
        vbe_update();
    }

    panic_halt();
}

static void wait_8042_input_clear() {
    for (uint32_t i = 0; i < 0x10000U; i++) {
        if ((inb(0x64) & 0x02U) == 0U) return;
    }
}

static void reboot_system() {
    serial_write_line("[sys] reboot requested");
    vga_println("Rebooting...");
    asm volatile("cli");

    wait_8042_input_clear();
    outb(0x64, 0xFE);

    outb(0xCF9, 0x02);
    outb(0xCF9, 0x06);

    {
        struct {
            uint16_t limit;
            uint32_t base;
        } __attribute__((packed)) null_idtr = {0, 0};
        asm volatile("lidt %0" : : "m"(null_idtr));
        asm volatile("int3");
    }

    panic_halt();
}

static void poweroff_system() {
    serial_write_line("[sys] poweroff requested");
    vga_println("Powering off...");
    asm volatile("cli");

    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);

    panic_halt();
}

static void print_kernel_log() {
    static char log_buf[4096];
    int len = serial_copy_ring_buffer(log_buf, sizeof(log_buf));

    if (len <= 0) {
        vga_println("Kernel log is empty.");
        return;
    }

    vga_print(log_buf);
    if (len > 0 && log_buf[len - 1] != '\n') vga_println("");
}

static void print_pci_id_line(const char* label, const pci_device_info_t* dev) {
    char buf[11];

    vga_print(label);
    if (!dev) {
        vga_println("none");
        return;
    }

    vga_print_int_hex((uint32_t)dev->vendor_id, buf);
    vga_print(buf + 6);
    vga_print(":");
    vga_print_int_hex((uint32_t)dev->device_id, buf);
    vga_print(buf + 6);
    vga_print(" @ ");
    vga_print_int_hex((uint32_t)dev->bus, buf);
    vga_print(buf + 8);
    vga_print(":");
    vga_print_int_hex((uint32_t)dev->slot, buf);
    vga_print(buf + 8);
    vga_print(".");
    vga_print_int_hex((uint32_t)dev->func, buf);
    vga_print(buf + 8);
    vga_println("");
}

static void print_pci_irq_line(const char* label, const pci_device_info_t* dev) {
    pci_irq_route_t route;

    vga_print(label);
    if (!dev || pci_decode_irq(dev, &route) != 0) {
        vga_println("none");
        return;
    }

    vga_print(pci_irq_pin_name(route.irq_pin));
    if (route.routed) {
        vga_print(" -> IRQ ");
        vga_print_int(route.irq_line);
        vga_println(route.masked ? " (masked)" : " (enabled)");
    } else {
        vga_println(" -> unrouted");
    }
}

static void print_hardware_info() {
    const cpu_info_t* cpu = cpu_get_info();
    pci_device_info_t devices[64];
    const pci_device_info_t* storage_dev = 0;
    const pci_device_info_t* network_dev = 0;
    const pci_device_info_t* display_dev = 0;
    const pci_device_info_t* usb_dev = 0;
    int pci_total;
    int storage_count = 0;
    int network_count = 0;
    int display_count = 0;
    int usb_count = 0;
    int bridge_count = 0;
    int other_count = 0;
    net_ipv4_config_t netcfg;
    char buf[64];

    pci_total = pci_enumerate(devices, 64);
    if (pci_total > 64) pci_total = 64;
    for (int i = 0; i < pci_total; i++) {
        const pci_device_info_t* dev = &devices[i];
        if (dev->class_code == 0x01) {
            storage_count++;
            if (!storage_dev) storage_dev = dev;
        } else if (dev->class_code == 0x02) {
            network_count++;
            if (!network_dev) network_dev = dev;
        } else if (dev->class_code == 0x03) {
            display_count++;
            if (!display_dev) display_dev = dev;
        } else if (dev->class_code == 0x0C && dev->subclass == 0x03) {
            usb_count++;
            if (!usb_dev) usb_dev = dev;
        } else if (dev->class_code == 0x06) {
            bridge_count++;
        } else {
            other_count++;
        }
    }

    vga_println("Hardware Info");
    vga_print("  CPU Vendor : ");
    vga_println(cpu->vendor);
    vga_print("  CPUID      : ");
    vga_println(cpu->cpuid_supported ? "yes" : "no");
    vga_print("  SSE        : ");
    vga_println(cpu->sse_enabled ? "enabled" : (cpu->sse_supported ? "supported, disabled" : "not supported"));
    vga_print("  PSE        : ");
    vga_println(cpu->pse_supported ? "yes" : "no");
    vga_print("  APIC       : ");
    vga_println(cpu->apic_supported ? "yes" : "no");
    vga_print("  TSC        : ");
    vga_println(cpu->tsc_supported ? "yes" : "no");
    vga_print("  Video      : ");
    if (screen_is_graphics_enabled()) {
        vga_print_int(vbe_get_width());
        vga_print("x");
        vga_print_int(vbe_get_height());
        vga_print(" @ ");
        vga_print_int(vbe_get_bpp());
        vga_println("bpp");
    } else {
        vga_println("text-mode fallback");
    }
    vga_print("  Managed RAM: ");
    vga_print_int((int)(paging_total_frames() / 256U));
    vga_println(" MB");
    vga_print("  PCI Count  : ");
    vga_print_int(pci_device_count());
    vga_println("");
    vga_println("  PCI Classes:");
    vga_print("    Storage : ");
    vga_print_int(storage_count);
    vga_println("");
    vga_print("    Network : ");
    vga_print_int(network_count);
    vga_println("");
    vga_print("    Display : ");
    vga_print_int(display_count);
    vga_println("");
    vga_print("    USB     : ");
    vga_print_int(usb_count);
    vga_println("");
    vga_print("    Bridge  : ");
    vga_print_int(bridge_count);
    vga_println("");
    vga_print("    Other   : ");
    vga_print_int(other_count);
    vga_println("");
    print_pci_id_line("  First Storage : ", storage_dev);
    if (storage_dev) {
        vga_print("  Storage Ifc   : ");
        vga_println(pci_storage_controller_name(storage_dev));
        print_pci_irq_line("  Storage IRQ   : ", storage_dev);
    }
    vga_print("  Storage Path  : ");
    vga_println(storage_backend_name());
    vga_print("  Storage Base  : ");
    vga_print_int((int)storage_volume_base_lba());
    vga_print(" (");
    if (storage_volume_scheme() == STORAGE_PARTITION_SCHEME_GPT) vga_print("GPT");
    else if (storage_volume_scheme() == STORAGE_PARTITION_SCHEME_MBR) vga_print("MBR");
    else vga_print("RAW");
    vga_print(")");
    vga_println("");
    print_pci_id_line("  First Network : ", network_dev);
    if (network_dev) {
        print_pci_irq_line("  Network IRQ   : ", network_dev);
    }
    print_pci_id_line("  First Display : ", display_dev);
    print_pci_id_line("  First USB     : ", usb_dev);
    if (net_get_ipv4_config(&netcfg) == 0 && netcfg.available) {
        vga_print("  Network    : ");
        vga_println(netcfg.configured ? "available + configured" : "available + unconfigured");
        vga_print("  IP         : ");
        vga_print_int((int)((netcfg.ip_addr >> 24) & 0xFF));
        vga_print(".");
        vga_print_int((int)((netcfg.ip_addr >> 16) & 0xFF));
        vga_print(".");
        vga_print_int((int)((netcfg.ip_addr >> 8) & 0xFF));
        vga_print(".");
        vga_print_int((int)(netcfg.ip_addr & 0xFF));
        vga_println("");
    } else {
        vga_println("  Network    : unavailable");
    }
    vga_print("  Uptime     : ");
    vga_print_int((int)(timer_ticks / 100));
    vga_println("s");
    vga_print("  CPUID max  : ");
    vga_print_int_hex(cpu->max_basic_leaf, buf);
    vga_println(buf);
}

void gpf_handler(trap_frame_t* frame) {
    serial_write_line("");
    serial_write_line("[panic] general protection fault");
    panic_serial_hex("error", frame->error_code);
    panic_serial_hex("eip", frame->eip);
    panic_serial_hex("cs", frame->cs);
    panic_serial_hex("user_esp", frame->user_esp);
    panic_serial_hex("user_ss", frame->user_ss);
    panic_log_current_process();
    process_debug_dump("gpf");

    if (!kernel_graphics_ready) {
        panic_text_exception("!!! NARC-OS GPF (RING 3 CRASH) !!!", 0, 0, frame);
        panic_halt();
    }

    vbe_clear(0x880000); // Red
    vbe_draw_string(20, 20, "!!! NARC-OS GPF (RING 3 CRASH) !!!", 0xFFFFFF);
    
    char buf[64];
    // GS, FS, ES, DS, EDI, ESI, EBP, ESP?, EBX, EDX, ECX, EAX
    const char* reg_names[] = {"GS", "FS", "ES", "DS", "EDI", "ESI", "EBP", "ESP_U", "EBX", "EDX", "ECX", "EAX"};
    uint32_t* raw = (uint32_t*)frame;
    
    for(int i=0; i<12; i++) {
        vbe_draw_string(20, 50 + (i*15), reg_names[i], 0xFFFFFF);
        vga_print_int_hex(raw[i], buf);
        vbe_draw_string(80, 50 + (i*15), buf, 0xCCCCCC);
    }
    
    vbe_draw_string(250, 50, "HW-ERR:", 0xFFFFFF);
    vga_print_int_hex(frame->error_code, buf);
    vbe_draw_string(350, 50, buf, 0xFFFF00);

    vbe_draw_string(250, 65, "HW-EIP:", 0xFFFFFF);
    vga_print_int_hex(frame->eip, buf);
    vbe_draw_string(350, 65, buf, 0xFFFF00);

    vbe_draw_string(250, 80, "HW-CS:", 0xFFFFFF);
    vga_print_int_hex(frame->cs, buf);
    vbe_draw_string(350, 80, buf, 0xFFFF00);

    vbe_draw_string(250, 95, "HW-ESP:", 0xFFFFFF);
    vga_print_int_hex(frame->user_esp, buf);
    vbe_draw_string(350, 95, buf, 0xFFFF00);

    vbe_draw_string(250, 110, "HW-SS:", 0xFFFFFF);
    vga_print_int_hex(frame->user_ss, buf);
    vbe_draw_string(350, 110, buf, 0xFFFF00);

    vbe_update();
    panic_halt();
}

void page_fault_handler(trap_frame_t* frame) {
    panic_log_current_process();
    process_debug_dump("page-fault");
    panic_simple_exception("page fault", "!!! NARC-OS PAGE FAULT !!!", 0x1A0000,
                           "Fault Addr:", read_cr2(), frame);
}

void invalid_opcode_handler(trap_frame_t* frame) {
    panic_simple_exception("invalid opcode", "!!! NARC-OS INVALID OPCODE !!!", 0x341400,
                           0, 0, frame);
}

void stack_fault_handler(trap_frame_t* frame) {
    panic_simple_exception("stack fault", "!!! NARC-OS STACK FAULT !!!", 0x301400,
                           0, 0, frame);
}

void user_code_test_logic() {
    // Super simple loop. No strings, no complex stack.
    while(1) {
        asm volatile (
            "mov $4, %%eax\n" // SYS_GUI_UPDATE
            "int $0x80"
            : : : "eax"
        );
        for(int i=0; i<50000; i++) asm volatile("nop");
    }
}

void vbe_compose_scene_basic() {
    // This is a simplified version of the composer for testing
    // It's called from SYSCALL_GUI_UPDATE to bypass the blocked kmain loop
    extern window_t windows[MAX_WINDOWS];
    extern int window_count, active_window_idx;
    extern int current_dir_index;
    int mx = get_mouse_x();
    int my = get_mouse_y();
    
    // Use the global state to redraw the screen
    vbe_compose_scene(windows, window_count, active_window_idx, 0, current_dir_index, -1, mx, my, 0, 0, 0, 0, 0, -1);
    vbe_prepare_frame_from_composition();
    vbe_render_mouse(mx, my);
}

static int run_usermode_test_command(void) {
    extern void jump_to_usermode_v9(uint32_t eip, uint32_t esp, uint32_t lfb);
    extern void usermode_entry_gate();
    uint32_t user_esp = 0x90000;
    uint32_t lfb_addr = *(uint32_t*)(0x6100 + 40);
    uint32_t target_eip = (uint32_t)usermode_entry_gate;
    uint32_t* magic_ptr = (uint32_t*)(target_eip - 4);
    char buf[64];

    vga_println("Launching Secure User Mode Test V12 (Final Victory)...");
    vga_print("Target EIP Sym: ");
    vga_print_int_hex(target_eip, buf);
    vga_println(buf);

    if (*magic_ptr != 0xDEADC0DE) {
        vga_println("CRITICAL: MAGIC NUMBER MISMATCH!");
        return -1;
    }

    vga_println("Magic Recognized. Transitioning to Ring 3...");
    vga_println("Verification: If the heartbeat pixel is rotating and the");
    vga_println("mouse is responsive, the transition was successful!");

    set_tss_stack(KERNEL_BOOT_STACK_TOP);
    jump_to_usermode_v9(target_eip, user_esp, lfb_addr);
    return 0;
}

static int kernel_snapshot_contains_pid(int pid) {
    process_snapshot_entry_t entries[16];
    int count = process_snapshot(entries, (int)(sizeof(entries) / sizeof(entries[0])));

    if (count < 0) return 0;
    for (int i = 0; i < count; i++) {
        if (entries[i].pid == pid) return 1;
    }
    return 0;
}

static void kernel_waitpid_test_child(void* arg) {
    (void)arg;
    while (!kernel_waitpid_test_release) process_yield();
}

static void kernel_pipe_test_writer(void* arg) {
    process_t* current = process_current();
    int write_fd = (int)(uintptr_t)arg;
    uint8_t buffer[128];
    uint32_t remaining = kernel_pipe_test_state.target_bytes;

    kernel_pipe_test_state.writer_status = -1;
    if (!current) return;
    memset(buffer, 'P', sizeof(buffer));
    (void)fd_close(current, kernel_pipe_test_state.read_fd);

    while (remaining != 0U) {
        uint32_t chunk_len = remaining;
        int rc;

        if (chunk_len > sizeof(buffer)) chunk_len = sizeof(buffer);
        rc = fd_write(current, write_fd, buffer, chunk_len);
        if (rc <= 0) {
            kernel_pipe_test_state.writer_status = -2;
            return;
        }
        kernel_pipe_test_state.bytes_written += (uint32_t)rc;
        remaining -= (uint32_t)rc;
        process_yield();
    }

    kernel_pipe_test_state.writer_status = fd_close(current, write_fd) == 0 ? 0 : -3;
}

static void kernel_pipe_test_reader(void* arg) {
    process_t* current = process_current();
    int read_fd = (int)(uintptr_t)arg;
    uint8_t buffer[96];

    kernel_pipe_test_state.reader_status = -1;
    if (!current) return;
    (void)fd_close(current, kernel_pipe_test_state.write_fd);

    for (;;) {
        int rc = fd_read(current, read_fd, buffer, sizeof(buffer));

        if (rc < 0) {
            kernel_pipe_test_state.reader_status = -2;
            return;
        }
        if (rc == 0) break;
        kernel_pipe_test_state.bytes_read += (uint32_t)rc;
        process_yield();
    }

    kernel_pipe_test_state.reader_status = fd_close(current, read_fd) == 0 ? 0 : -3;
}

static int run_process_model_selftest(void) {
    int pid;
    int status = -1;
    int wait_rc;

    kernel_waitpid_test_release = 0;
    pid = process_create_kernel("waitpid-test", kernel_waitpid_test_child, 0);
    if (pid < 0) {
        vga_println("proc_test: spawn failed.");
        return -1;
    }

    wait_rc = process_waitpid_sync_current(pid, WAITPID_FLAG_NOHANG, &status);
    if (wait_rc != 0) {
        kernel_waitpid_test_release = 1;
        (void)process_waitpid_sync_current(pid, 0U, 0);
        vga_println("proc_test: WAITPID_FLAG_NOHANG failed.");
        return -1;
    }

    kernel_waitpid_test_release = 1;
    wait_rc = process_waitpid_sync_current(pid, 0U, &status);
    if (wait_rc != pid || status != 0) {
        vga_println("proc_test: waitpid returned wrong result.");
        return -1;
    }

    wait_rc = process_waitpid_sync_current(pid, WAITPID_FLAG_NOHANG, &status);
    if (wait_rc != -1) {
        vga_println("proc_test: reaped child was still waitable.");
        return -1;
    }

    if (kernel_snapshot_contains_pid(pid)) {
        vga_println("proc_test: zombie cleanup failed.");
        return -1;
    }

    vga_println("proc_test: ok");
    return 0;
}

static int run_pipe_selftest(void) {
    process_t* current = process_current();
    int pipefd[2] = { -1, -1 };
    int reader_pid = -1;
    int writer_pid = -1;
    int wait_status = 0;

    if (!current) {
        vga_println("pipe_test: no current process.");
        return -1;
    }

    memset(&kernel_pipe_test_state, 0, sizeof(kernel_pipe_test_state));
    kernel_pipe_test_state.target_bytes = PIPE_BUFFER_SIZE * 3U + 73U;

    if (fd_pipe(current, pipefd) != 0) {
        vga_println("pipe_test: pipe creation failed.");
        return -1;
    }

    kernel_pipe_test_state.read_fd = pipefd[0];
    kernel_pipe_test_state.write_fd = pipefd[1];
    reader_pid = process_create_kernel("pipe-reader", kernel_pipe_test_reader, (void*)(uintptr_t)pipefd[0]);
    if (reader_pid < 0) goto fail;
    writer_pid = process_create_kernel("pipe-writer", kernel_pipe_test_writer, (void*)(uintptr_t)pipefd[1]);
    if (writer_pid < 0) goto fail;

    (void)fd_close(current, pipefd[0]);
    pipefd[0] = -1;
    (void)fd_close(current, pipefd[1]);
    pipefd[1] = -1;

    if (process_waitpid_sync_current(writer_pid, 0U, &wait_status) != writer_pid) goto fail;
    if (process_waitpid_sync_current(reader_pid, 0U, &wait_status) != reader_pid) goto fail;

    if (kernel_pipe_test_state.writer_status != 0 || kernel_pipe_test_state.reader_status != 0) goto fail;
    if (kernel_pipe_test_state.bytes_written != kernel_pipe_test_state.target_bytes) goto fail;
    if (kernel_pipe_test_state.bytes_read != kernel_pipe_test_state.target_bytes) goto fail;
    if (kernel_pipe_test_state.bytes_read != kernel_pipe_test_state.bytes_written) goto fail;

    vga_println("pipe_test: ok");
    return 0;

fail:
    if (pipefd[0] >= 0) (void)fd_close(current, pipefd[0]);
    if (pipefd[1] >= 0) (void)fd_close(current, pipefd[1]);
    if (writer_pid > 0) (void)process_waitpid_sync_current(writer_pid, 0U, 0);
    if (reader_pid > 0) (void)process_waitpid_sync_current(reader_pid, 0U, 0);
    vga_println("pipe_test: failed.");
    return -1;
}

int kernel_run_privileged_command(int cmd, const char* arg) {
    const char* value = arg ? arg : "";
    int graphics_mode = screen_is_graphics_enabled();

    switch (cmd) {
        case PRIV_CMD_SNAKE:
            if (!graphics_mode) return -1;
            open_snake_window();
            vga_println("Snake launched in Ring 3.");
            return 0;
        case PRIV_CMD_SETTINGS:
            if (!graphics_mode) return -1;
            open_settings_window();
            vga_println("Settings opened.");
            return 0;
        case PRIV_CMD_EDIT:
            if (value[0] == '\0') return -1;
            editor_start(value);
            clear_screen();
            return 0;
        case PRIV_CMD_MEM:
            print_memory_info();
            return 0;
        case PRIV_CMD_MALLOC_TEST:
            malloc_test();
            return 0;
        case PRIV_CMD_USERMODE_TEST:
            if (!graphics_mode) return -1;
            return run_usermode_test_command();
        case PRIV_CMD_HWINFO:
            print_hardware_info();
            return 0;
        case PRIV_CMD_PCI:
            pci_print_devices();
            return 0;
        case PRIV_CMD_STORAGE:
            pci_print_storage_devices();
            storage_print_status();
            return 0;
        case PRIV_CMD_LOG:
            print_kernel_log();
            return 0;
        case PRIV_CMD_REBOOT:
            reboot_system();
            return 0;
        case PRIV_CMD_POWEROFF:
            poweroff_system();
            return 0;
        case PRIV_CMD_PROC_DUMP:
            process_debug_dump(value[0] != '\0' ? value : "manual");
            vga_println("process dump written to serial.");
            return 0;
        case PRIV_CMD_PROC_TEST:
            return run_process_model_selftest();
        case PRIV_CMD_PIPE_TEST:
            return run_pipe_selftest();
        default:
            return -1;
    }
}

void execute_command(char* cmd) {
    int status;
    if (!cmd) return;
    if (strcmp(cmd, "http") == 0 || strncmp(cmd, "http ", 5) == 0) {
        const char* arg = cmd + 4;
        while (*arg == ' ') arg++;
        (void)net_http_command(arg);
        return;
    }
    status = run_user_shell_command(cmd);
    if (status < 0) {
        vga_print_color("shell command failed: ", 0x0C);
        vga_print_int(status);
        vga_println("");
    }
}

extern char cmd_to_execute[128];
extern volatile int cmd_ready;
extern int current_dir_index;
extern void get_current_dir_name(char* buf);

void print_prompt() {
    vga_print_color("root@narc:", 0x0A);
    char path[256];
    fs_get_current_path(path, sizeof(path));
    vga_print_color(path, 0x0B);
    vga_print_color("$ ", 0x0A);
}

static void print_text_fallback_banner() {
    vga_print_color("\n  NarcOs Text Fallback Mode\n", 0x0E);
    vga_print_color("  ========================\n", 0x0E);
    vga_println("  Graphics init unavailable; continuing on VGA text console.");
    vga_println("  GUI apps are disabled in this mode.");
    print_prompt();
}

static void console_process_main(void) {
    for (;;) {
        if (cmd_ready) {
            execute_command(cmd_to_execute);
            cmd_ready = 0;
            print_prompt();
        }
        process_poll();
        asm volatile("hlt");
        process_poll();
    }
}

static void console_process_entry(void* arg) {
    (void)arg;
    console_process_main();
}

static void desktop_process_main(void) {
    int start_menu_visible = 0;
    int desk_dir_idx = -1;
    int ctx_visible = 0, ctx_x = 0, ctx_y = 0;
    const char* ctx_items_desk[] = {"New File", "New Folder", "Refresh"};
    const char* ctx_items_exp[]  = {"Open", "Open With", "Rename", "Delete", "New File", "New Folder", "Refresh"};
    const char* ctx_items_pad[]  = {"Save", "Close"};
    const char** ctx_items = ctx_items_desk;
    int ctx_count = 3, ctx_selected = -1;
    fs_change_dir("/");
    fs_change_dir("home");
    fs_change_dir("user");
    fs_change_dir("Desktop");
    desk_dir_idx = current_dir_index;

    uint32_t last_clock_tick = 0;
    uint32_t last_click_tick = 0;
    int last_click_target = DESKTOP_CLICK_TARGET_NONE;
    int last_mx = 0, last_my = 0, last_lp = 0, last_rp = 0;
    int mx = get_mouse_x();
    int my = get_mouse_y();
    int lp = mouse_left_pressed();
    
    int dragging_idx = -1;
    int drag_off_x = 0, drag_off_y = 0;
    int drag_file_idx = -1;
    uint32_t last_snk_tick = 0;
    int snk_dir = 3;

    vbe_compose_scene(windows, window_count, active_window_idx, start_menu_visible, desk_dir_idx, drag_file_idx, mx, my, ctx_visible, ctx_x, ctx_y, ctx_items, ctx_count, ctx_selected);
    vbe_prepare_frame_from_composition();
    vbe_render_mouse(mx, my);
    vbe_update();
    last_mx = mx;
    last_my = my;
    last_lp = lp;
    while (1) {
        mx = get_mouse_x();
        my = get_mouse_y();
        lp = mouse_left_pressed();
        int rp = mouse_right_pressed();
        int mouse_moved = mouse_consume_moved();
        if (!lp && drag_file_idx != -1) {
            int eidx = nwm_get_idx_by_type(WIN_TYPE_EXPLORER);
            if (eidx != -1 && windows[eidx].visible) {
                int wx = windows[eidx].x;
                int wy = windows[eidx].y;
                int ww = windows[eidx].w;
                int client_x = wx + 8;
                int client_y = wy + 40;
                int client_w = ww - 16;
                int sidebar_w = 136;
                int content_x = client_x + sidebar_w + 14;
                int content_w = client_w - sidebar_w - 26;
                int panel_y = client_y + 36;
                int list_y = panel_y + 48;
                int row_h = 54;
                if (mx >= client_x + 12 && mx <= client_x + 122) {
                    if (my >= panel_y + 34 && my <= panel_y + 56) explorer_move_selected_to(-1);
                    else if (my >= panel_y + 62 && my <= panel_y + 84) explorer_move_selected_to(desk_dir_idx);
                    else if (my >= panel_y + 90 && my <= panel_y + 112) {
                        int home_idx = fs_find_node("/home/user");
                        if (home_idx >= 0) explorer_move_selected_to(home_idx);
                    }
                } else if (mx >= content_x + 16 && mx <= content_x + content_w - 16 && my >= list_y) {
                    int hit_slot = (my - list_y) / row_h;
                    int current_slot = 0;
                    for (int i = 0; i < MAX_FILES; i++) {
                        if (dir_cache[i].flags != 0 && dir_cache[i].parent_index == user_explorer_state.current_dir) {
                            if (current_slot == hit_slot && dir_cache[i].flags == FS_NODE_DIR) {
                                explorer_move_selected_to(i);
                                break;
                            }
                            current_slot++;
                        }
                    }
                }
            }
            drag_file_idx = -1;
            exp_drag_idx = -1;
            exp_drag_source_dir = -1;
            exp_drag_armed = 0;
            gui_needs_redraw = 1;
        }
        if (rp != last_rp && rp) {
            if (explorer_modal_active()) {
                explorer_cancel_modal();
                goto process_done;
            }
            ctx_visible = 1; ctx_x = mx; ctx_y = my; ctx_selected = -1;
            int pidx = nwm_get_idx_by_type(WIN_TYPE_NARCPAD);
            int eidx = nwm_get_idx_by_type(WIN_TYPE_EXPLORER);
            if (pidx != -1 && windows[pidx].visible && mx >= windows[pidx].x && mx <= windows[pidx].x + windows[pidx].w && my >= windows[pidx].y && my <= windows[pidx].y + windows[pidx].h) {
                ctx_items = ctx_items_pad; ctx_count = 2;
            } else if (eidx != -1 && windows[eidx].visible && mx >= windows[eidx].x && mx <= windows[eidx].x + windows[eidx].w && my >= windows[eidx].y && my <= windows[eidx].y + windows[eidx].h) {
                ctx_items = ctx_items_exp; ctx_count = 7;
            } else {
                ctx_items = ctx_items_desk; ctx_count = 3;
            }
            gui_needs_redraw = 1;
        }
        if (lp != last_lp && lp) {
            if (explorer_modal_active()) {
                int sw = (int)vbe_get_width();
                int sh = (int)vbe_get_height();
                int dx = (sw - 320) / 2;
                int dy = (sh - 140) / 2;
                if (mx >= dx + 184 && mx <= dx + 234 && my >= dy + 108 && my <= dy + 126) explorer_cancel_modal();
                else if (mx >= dx + 242 && mx <= dx + 296 && my >= dy + 108 && my <= dy + 126) explorer_modal_submit();
                goto process_done;
            }
            if (ctx_visible) {
                 if (ctx_selected != -1) {
                     const char* cmd = ctx_items[ctx_selected];
                     if (strcmp(cmd, "Save") == 0) {
                         queue_user_narcpad_event(USER_NARCPAD_EVT_SAVE, 0);
                     } else if (strcmp(cmd, "Open") == 0) {
                         explorer_open_selected();
                     } else if (strcmp(cmd, "Open With") == 0) {
                         explorer_open_with_selected();
                     } else if (strcmp(cmd, "Rename") == 0) {
                         explorer_begin_rename_selected();
                     } else if (strcmp(cmd, "Delete") == 0) {
                         explorer_begin_delete_selected();
                     } else if (strcmp(cmd, "New File") == 0) {
                         explorer_create_in_dir(user_explorer_state.current_dir, 0);
                     } else if (strcmp(cmd, "New Folder") == 0) {
                         explorer_create_in_dir(user_explorer_state.current_dir, 1);
                     } else if (strcmp(cmd, "Close") == 0) {
                         int pidx = nwm_get_idx_by_type(WIN_TYPE_NARCPAD);
                         if (pidx != -1) windows[pidx].visible = 0;
                     } else if (strcmp(cmd, "Refresh") == 0) {
                         if (user_explorer_running()) queue_user_explorer_event(USER_EXPLORER_EVT_REFRESH, 0);
                         gui_needs_redraw = 1;
                     }
                 }
                 ctx_visible = 0; gui_needs_redraw = 1;
                 goto process_done;
            }
            if (start_menu_visible && mx >= 20 && mx <= 240) {
                if (my >= 122 && my <= 146) {
                    open_terminal_window();
                    start_menu_visible = 0;
                    goto process_done;
                }
                if (my >= 154 && my <= 178) {
                    open_snake_window();
                    start_menu_visible = 0;
                    goto process_done;
                }
                if (my >= 186 && my <= 210) {
                    open_narcpad_window();
                    start_menu_visible = 0;
                    goto process_done;
                }
                if (my >= 218 && my <= 242) {
                    open_settings_window();
                    start_menu_visible = 0;
                    goto process_done;
                }
            }
            int click_target = DESKTOP_CLICK_TARGET_NONE;
            int preview_hit_win = -1;
            if (my > 35) {
                preview_hit_win = nwm_find_window_at(mx, my);
                if (preview_hit_win != -1 && windows[preview_hit_win].type == WIN_TYPE_EXPLORER) {
                    click_target = get_explorer_list_click_target(mx, my, &windows[preview_hit_win]);
                } else if (preview_hit_win == -1) {
                    click_target = get_desktop_click_target(mx, my);
                }
            }
            int double_click = (click_target != DESKTOP_CLICK_TARGET_NONE &&
                                click_target == last_click_target &&
                                timer_ticks - last_click_tick < DESKTOP_DOUBLE_CLICK_TICKS);
            last_click_target = click_target;
            last_click_tick = timer_ticks;
            if (my <= 35) {
                if (mx >= 5 && mx <= 89) { start_menu_visible = !start_menu_visible; gui_needs_redraw = 1; }
                else if (mx >= (int)vbe_get_width() - 104 && mx <= (int)vbe_get_width() - 14) {
                    open_settings_window();
                    start_menu_visible = 0;
                }
                else if (mx >= 96 && mx <= 136) {
                    int tidx = nwm_get_idx_by_type(WIN_TYPE_TERMINAL);
                    if (tidx != -1) {
                        if (!windows[tidx].visible) { windows[tidx].visible = 1; windows[tidx].minimized = 0; nwm_bring_to_front(tidx); }
                        else if (windows[tidx].minimized) { windows[tidx].minimized = 0; nwm_bring_to_front(tidx); }
                        else if (tidx == active_window_idx) { windows[tidx].minimized = 1; }
                        else { nwm_bring_to_front(tidx); }
                        gui_needs_redraw = 1;
                    }
                }
                else {
                    int clicked_slot = (mx - 148) / 112;
                    if (mx >= 148 && clicked_slot >= 0) {
                        int current_slot = 0;
                        for (int i = 0; i < window_count; i++) {
                            if (!windows[i].visible) continue;
                            if (current_slot == clicked_slot) {
                                if (windows[i].minimized) { windows[i].minimized = 0; nwm_bring_to_front(i); }
                                else if (i == active_window_idx) { windows[i].minimized = 1; }
                                else { nwm_bring_to_front(i); }
                                gui_needs_redraw = 1; break;
                            }
                            current_slot++;
                        }
                    }
                }
            } else {
                int hit_win = nwm_find_window_at(mx, my);
                if (hit_win != -1) {
                    nwm_bring_to_front(hit_win);
                    hit_win = active_window_idx;
                    
                    if (my <= windows[hit_win].y + 34) {
                        if (mx >= windows[hit_win].x + windows[hit_win].w - 24) {
                            if (windows[hit_win].type == WIN_TYPE_SNAKE) stop_user_snake();
                            windows[hit_win].visible = 0; gui_needs_redraw = 1;
                        } else if (mx >= windows[hit_win].x + windows[hit_win].w - 44) {
                            windows[hit_win].minimized = 1; gui_needs_redraw = 1;
                        } else {
                            dragging_idx = hit_win;
                            drag_off_x = mx - windows[hit_win].x;
                            drag_off_y = my - windows[hit_win].y;
                        }
                    } else if (windows[hit_win].type == WIN_TYPE_EXPLORER) {
                        int wx = windows[hit_win].x, wy = windows[hit_win].y;
                        int ww = windows[hit_win].w;
                        int client_x = wx + 8;
                        int client_y = wy + 40;
                        int client_w = ww - 16;
                        int breadcrumb_y = client_y;
                        int sidebar_w = 136;
                        int content_x = client_x + sidebar_w + 14;
                        int content_w = client_w - sidebar_w - 26;
                        int panel_y = client_y + 36;
                        int list_y = panel_y + 48;
                        int row_h = 54;
                        if (my >= breadcrumb_y && my <= breadcrumb_y + 28) {
                            if (mx >= client_x && mx <= client_x + client_w) {
                                explorer_open_dir(-1);
                            }
                        }
                        else if (my >= panel_y + 8 && my <= panel_y + 28) {
                            if (mx >= content_x + 12 && mx <= content_x + 58) {
                                if (user_explorer_state.prev_dir != -1) explorer_open_dir(user_explorer_state.prev_dir);
                            } else if (mx >= content_x + 66 && mx <= content_x + 104) {
                                if (user_explorer_state.current_dir != -1) explorer_open_dir(dir_cache[user_explorer_state.current_dir].parent_index);
                            } else if (mx >= content_x + 112 && mx <= content_x + 182) {
                                if (explorer_create_in_dir(user_explorer_state.current_dir, 0) == 0) gui_needs_redraw = 1;
                            } else if (mx >= content_x + 190 && mx <= content_x + 274) {
                                if (explorer_create_in_dir(user_explorer_state.current_dir, 1) == 0) gui_needs_redraw = 1;
                            } else if (mx >= content_x + 282 && mx <= content_x + 342) {
                                explorer_begin_rename_selected();
                            } else if (mx >= content_x + 350 && mx <= content_x + 404) {
                                explorer_begin_delete_selected();
                            } else if (mx >= content_x + content_w - 76 && mx <= content_x + content_w - 16) {
                                if (user_explorer_running()) queue_user_explorer_event(USER_EXPLORER_EVT_REFRESH, 0);
                                gui_needs_redraw = 1;
                            }
                        }
                        else if (mx >= client_x + 12 && mx <= client_x + 122) {
                            if (my >= client_y + 76 && my <= client_y + 98) {
                                explorer_open_dir(-1);
                            } else if (my >= client_y + 104 && my <= client_y + 126) {
                                explorer_open_dir(desk_dir_idx);
                            } else if (my >= client_y + 132 && my <= client_y + 154) {
                                {
                                    int home_idx = fs_find_node("/home/user");
                                    if (home_idx >= 0) explorer_open_dir(home_idx);
                                }
                            }
                        }
                        else if (double_click) {
                            int card_x0 = content_x + 16;
                            int card_x1 = content_x + content_w - 16;
                            if (mx >= card_x0 && mx <= card_x1 && my >= list_y) {
                                int hit_slot = (my - list_y) / row_h;
                                int current_slot = 0;
                                for (int i = 0; i < MAX_FILES; i++) {
                                    if (dir_cache[i].flags != 0 && dir_cache[i].parent_index == user_explorer_state.current_dir) {
                                        if (current_slot == hit_slot) {
                                            queue_user_explorer_event(USER_EXPLORER_EVT_SELECT_IDX, i);
                                            exp_drag_idx = i;
                                            exp_drag_source_dir = user_explorer_state.current_dir;
                                            exp_drag_armed = 1;
                                            explorer_open_selected();
                                            break;
                                        }
                                        current_slot++;
                                    }
                                }
                            }
                        } else {
                            int card_x0 = content_x + 16;
                            int card_x1 = content_x + content_w - 16;
                            if (mx >= card_x0 && mx <= card_x1 && my >= list_y) {
                                int hit_slot = (my - list_y) / row_h;
                                int current_slot = 0;
                                queue_user_explorer_event(USER_EXPLORER_EVT_SELECT_IDX, -1);
                                exp_drag_idx = -1;
                                exp_drag_source_dir = -1;
                                exp_drag_armed = 0;
                                for (int i = 0; i < MAX_FILES; i++) {
                                    if (dir_cache[i].flags != 0 && dir_cache[i].parent_index == user_explorer_state.current_dir) {
                                        if (current_slot == hit_slot) {
                                            queue_user_explorer_event(USER_EXPLORER_EVT_SELECT_IDX, i);
                                            exp_drag_idx = i;
                                            exp_drag_source_dir = user_explorer_state.current_dir;
                                            exp_drag_armed = 1;
                                            gui_needs_redraw = 1;
                                            break;
                                        }
                                        current_slot++;
                                    }
                                }
                            }
                        }
                    } else if (windows[hit_win].type == WIN_TYPE_SETTINGS) {
                        if (settings_handle_click(&windows[hit_win], mx, my)) goto process_done;
                    }
                } else {
                    if (start_menu_visible && (mx > 200 || my > 335)) { start_menu_visible = 0; gui_needs_redraw = 1; }
                    if (mx >= 20 && mx <= 60) {
                        if (my >= 60 && my <= 110 && double_click) { 
                            open_explorer_window(desk_dir_idx);
                        }
                        else if (my >= 300 && my <= 350 && double_click) { 
                            open_snake_window();
                        }
                        else if (my >= 140 && double_click) {
                            int row_idx = (my - 140) / 80;
                            int current_row = 0;
                            for (int i = 0; i < MAX_FILES; i++) {
                                if (dir_cache[i].flags != 0 && dir_cache[i].parent_index == desk_dir_idx) {
                                    if (current_row == row_idx) {
                                        if (dir_cache[i].flags == 2) {
                                            open_explorer_window(i);
                                        } else {
                                            char path[256];
                                            fs_get_path_by_index(i, path, sizeof(path));
                                            open_file_in_narcpad_by_path(path);
                                        }
                                        break;
                                    }
                                    current_row++;
                                }
                            }
                        }
                    }
                }
            }
        } else if (!lp) {
            dragging_idx = -1;
        }

        if (dragging_idx != -1) {
            uint32_t sw = vbe_get_width();
            uint32_t sh = vbe_get_height();
            int win_w = windows[dragging_idx].w;
            int new_x = mx - drag_off_x;
            int new_y = my - drag_off_y;

            if (new_y < 35) new_y = 35;
            if (new_y > (int)sh - 20) new_y = (int)sh - 20;
            if (new_x < -(win_w - 40)) new_x = -(win_w - 40);
            if (new_x > (int)sw - 40) new_x = (int)sw - 40;

            if (windows[dragging_idx].x != new_x || windows[dragging_idx].y != new_y) {
                windows[dragging_idx].x = new_x;
                windows[dragging_idx].y = new_y;
                gui_needs_redraw = 1;
            }
        }
        if (lp && exp_drag_armed && exp_drag_idx != -1 && (mx != last_mx || my != last_my)) {
            if (drag_file_idx != exp_drag_idx || mouse_moved) {
                drag_file_idx = exp_drag_idx;
                gui_needs_redraw = 1;
            }
        }
        
        process_done: (void)0;
        int sidx = nwm_get_idx_by_type(WIN_TYPE_SNAKE);
        if (sidx != -1 && windows[sidx].visible && !user_snake_running() && !snk_dead && (timer_ticks - last_snk_tick > 10)) {
            last_snk_tick = timer_ticks;
            if (snk_next_dir != -1) {
                if (snk_next_dir == 6) { windows[sidx].visible = 0; snk_dead = 0; }
                else if (snk_next_dir == 5) {
                    snk_len = 5; snk_score = 0; snk_dead = 0;
                    snk_px[0] = 10; snk_py[0] = 10;
                    snk_dir = 3;
                }
                else if (!((snk_dir == 0 && snk_next_dir == 1) || (snk_dir == 1 && snk_next_dir == 0) ||
                      (snk_dir == 2 && snk_next_dir == 3) || (snk_dir == 3 && snk_next_dir == 2))) {
                    snk_dir = snk_next_dir;
                }
                snk_next_dir = -1;
            }

            for (int i = snk_len - 1; i > 0; i--) { snk_px[i] = snk_px[i-1]; snk_py[i] = snk_py[i-1]; }
            if (snk_dir == 0) snk_py[0]--; 
            if (snk_dir == 1) snk_py[0]++;
            if (snk_dir == 2) snk_px[0]--; 
            if (snk_dir == 3) snk_px[0]++;
            if (snk_px[0] < 0 || snk_px[0] >= 39 || snk_py[0] < 0 || snk_py[0] >= 29) snk_dead = 1;
            for (int i = 1; i < snk_len; i++) if (snk_px[0] == snk_px[i] && snk_py[0] == snk_py[i]) snk_dead = 1;
            if (snk_px[0] == apple_x && snk_py[0] == apple_y) {
                if (snk_len < 100) snk_len++;
                snk_score += 10;
                if (snk_score > snk_best) snk_best = snk_score;
                apple_x = (timer_ticks % 37) + 1; apple_y = (timer_ticks % 27) + 1;
            }
            gui_needs_redraw = 1;
        }
        if (timer_ticks - last_clock_tick >= 100) {
            read_rtc(); last_clock_tick = timer_ticks; gui_needs_redraw = 1;
        }
        run_user_tasks();
        if (gui_needs_redraw || lp != last_lp || rp != last_rp || cmd_ready) {
            vbe_compose_scene(windows, window_count, active_window_idx, start_menu_visible, desk_dir_idx, drag_file_idx, mx, my, ctx_visible, ctx_x, ctx_y, ctx_items, ctx_count, ctx_selected);
            vbe_prepare_frame_from_composition();
            vbe_render_mouse(mx, my);
            wait_vsync();
            vbe_update();
            last_mx = mx; last_my = my; last_lp = lp; last_rp = rp;
            gui_needs_redraw = 0;
        } else if (mouse_moved && (mx != last_mx || my != last_my)) {
            vbe_present_cursor_fast(last_mx, last_my, mx, my);
            last_mx = mx;
            last_my = my;
        }
        if (ctx_visible) {
            int new_sel = -1;
            if (mx >= ctx_x && mx <= ctx_x + 154 && my >= ctx_y && my <= ctx_y + ctx_count * 22 + 8) {
                new_sel = (my - (ctx_y + 4)) / 22;
                if (new_sel < 0 || new_sel >= ctx_count) new_sel = -1;
            }
            if (new_sel != ctx_selected) {
                ctx_selected = new_sel;
                gui_needs_redraw = 1;
            }
        }

        if (cmd_ready) {
            execute_command(cmd_to_execute); cmd_ready = 0; print_prompt();
        }
        process_poll();
        asm volatile("hlt");
        process_poll();
    }
}

static void desktop_process_entry(void* arg) {
    (void)arg;
    desktop_process_main();
}

static void service_process_main(void* arg) {
    (void)arg;
    for (;;) {
        net_poll();
        process_poll();
        asm volatile("hlt");
        process_poll();
    }
}

void kmain() {
    const cpu_info_t* cpu;
    int console_pid;
    int desktop_pid;
    int service_pid;

    serial_init();
    serial_write_line("[boot] kmain");
    cpu_init();
    cpu = cpu_get_info();
    if (!cpu->cpuid_supported || !cpu->pse_supported) {
        boot_fatal("Unsupported CPU detected.",
                   "Current kernel requires CPUID and 4 MB page support (PSE).");
    }
    serial_write_line("[boot] init_pic");
    init_pic();
    serial_write_line("[boot] init_pit");
    init_pit();
    serial_write_line("[boot] init_gdt");
    init_gdt();
    serial_write_line("[boot] load_idt");
    load_idt();
    serial_write_line("[boot] init_syscalls");
    init_syscalls();
    serial_write_line("[boot] init_paging");
    init_paging();
    serial_write("[boot] paging total_frames=");
    serial_write_hex32(paging_total_frames());
    serial_write(" used_frames=");
    serial_write_hex32(paging_used_frames());
    serial_write_char('\n');
    serial_write("[boot] kernel_stack base=");
    serial_write_hex32(paging_kernel_stack_base());
    serial_write(" size=");
    serial_write_hex32(paging_kernel_stack_size());
    serial_write_char('\n');
    paging_probe_kernel_vm();
    if (init_usermode() != 0) {
        boot_fatal("User memory initialization failed.",
                   "Ring 3 code/data alias mappings could not be established.");
    }
    usermode_debug_dump("post-usermode");
    init_keyboard();
    usermode_debug_dump("post-kbd");
    storage_init();
    usermode_debug_dump("post-storage");
    init_fs();
    usermode_debug_dump("post-fs");
    rtc_init_timezone();
    read_rtc();
    usermode_debug_dump("post-rtc");
    init_heap();
    usermode_debug_dump("post-heap");
    screen_set_graphics_enabled(0);
    if (boot_framebuffer_available()) {
        serial_write_line("[boot] init_vbe");
        init_vbe();
        usermode_debug_dump("post-init_vbe");
        kernel_graphics_ready = 1;
        screen_set_graphics_enabled(1);
        usermode_debug_dump("post-graphics-flag");
        init_mouse();
        usermode_debug_dump("post-init_mouse");
    } else {
        serial_write_line("[boot] framebuffer unavailable, using text fallback");
        kernel_graphics_ready = 0;
    }
    if (init_usermode() != 0) {
        boot_fatal("User memory reinitialization failed.",
                   "Ring 3 runtime state could not be restored after display setup.");
    }
    usermode_debug_dump("post-usermode-reinit");
    usermode_debug_dump("post-vbe");
    net_init();
    usermode_debug_dump("post-net");
    serial_write_line("[boot] process_init");
    process_init();
    usermode_debug_dump("post-procinit");
    console_pid = -1;
    desktop_pid = -1;
    if (screen_is_graphics_enabled()) {
        desktop_pid = process_create_kernel("desktop", desktop_process_entry, 0);
    } else {
        console_pid = process_create_kernel("console", console_process_entry, 0);
    }
    service_pid = process_create_kernel("service", service_process_main, 0);
    if ((screen_is_graphics_enabled() && desktop_pid < 0) ||
        (!screen_is_graphics_enabled() && console_pid < 0) ||
        service_pid < 0) {
        boot_fatal("Scheduler bootstrap failed.",
                   "Kernel tasks could not be created with the current memory map.");
    }
    if (screen_is_graphics_enabled()) {
        serial_write_line("[boot] desktop ready");
    } else {
        serial_write_line("[boot] text fallback ready");
    }
    clear_screen();
    if (screen_is_graphics_enabled()) {
        vga_print_color("\n  NarcOs GUI Initialized.\n", 0x0B);
        vga_print_color("  ========================\n", 0x0B);
        vga_println("  Welcome to NarcOs Desktop!");
        nwm_init_windows();
        print_prompt();
        vga_print("  Video Mode: ");
        vga_print_int(vbe_get_width());
        vga_print("x");
        vga_print_int(vbe_get_height());
        vga_print(" @ ");
        vga_print_int(vbe_get_bpp());
        vga_println("bpp");
    } else {
        print_text_fallback_banner();
    }
    usermode_debug_dump("pre-sched");
    serial_write_line("[boot] scheduler start");
    scheduler_start();
}
