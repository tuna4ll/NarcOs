#include <stdint.h>
#include "string.h"
#include "fs.h"
#include "rtc.h"
#include "editor.h"
#include "memory_alloc.h"
#include "vbe.h"
#include "mouse.h"
#include "gdt.h"
#include "syscall.h"
#include "usermode.h"
#include "net.h"

extern void outb(uint16_t port, uint8_t val);
extern void clear_screen();
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

window_t windows[MAX_WINDOWS];
int window_count = 0;
int active_window_idx = -1;

char pad_title[32] = "NarcPad";
char pad_content[1024] = {0};
int pad_file_idx = -1;
volatile int snk_next_dir = -1;
int exp_dir = -1;
int exp_selected = -1;
int exp_prev_dir = -1;
int exp_modal_mode = 0;
char exp_modal_input[32] = {0};
int exp_modal_input_len = 0;
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

    window_count = 4;
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

static void open_snake_window() {
    int idx = nwm_get_idx_by_type(WIN_TYPE_SNAKE);
    if (idx == -1) return;
    windows[idx].visible = 1;
    windows[idx].minimized = 0;
    nwm_bring_to_front(idx);
    if (!user_snake_running()) launch_user_snake();
    gui_needs_redraw = 1;
}

static void explorer_open_dir(int new_dir) {
    if (new_dir < -1 || new_dir >= MAX_FILES) return;
    if (new_dir >= 0 && dir_cache[new_dir].flags != FS_NODE_DIR) return;
    if (exp_dir != new_dir) exp_prev_dir = exp_dir;
    exp_dir = new_dir;
    exp_selected = -1;
    gui_needs_redraw = 1;
}

int explorer_modal_active() { return exp_modal_mode != 0; }

void explorer_cancel_modal() {
    exp_modal_mode = 0;
    exp_modal_input_len = 0;
    exp_modal_input[0] = '\0';
    gui_needs_redraw = 1;
}

static void explorer_build_path(int dir_idx, char* out, int out_len) {
    int chain[16];
    int count = 0;
    int pos = 0;
    if (!out || out_len <= 0) return;
    if (dir_idx < 0) {
        if (out_len >= 2) {
            out[0] = '/';
            out[1] = '\0';
        }
        return;
    }
    while (dir_idx >= 0 && count < 16) {
        chain[count++] = dir_idx;
        dir_idx = dir_cache[dir_idx].parent_index;
    }
    out[pos++] = '/';
    for (int i = count - 1; i >= 0 && pos < out_len - 1; i--) {
        for (int j = 0; dir_cache[chain[i]].name[j] != '\0' && pos < out_len - 1; j++) {
            out[pos++] = dir_cache[chain[i]].name[j];
        }
        if (i > 0 && pos < out_len - 1) out[pos++] = '/';
    }
    out[pos] = '\0';
}

static void append_text(char* dst, const char* src) {
    int pos = 0;
    while (dst[pos] != '\0') pos++;
    while (*src) dst[pos++] = *src++;
    dst[pos] = '\0';
}

static int explorer_create_in_dir(int dir_idx, int is_dir) {
    char base_path[256];
    char full_path[320];
    int n;
    explorer_build_path(dir_idx, base_path, sizeof(base_path));
    for (n = 1; n < 100; n++) {
        full_path[0] = '\0';
        append_text(full_path, base_path);
        if (!(base_path[0] == '/' && base_path[1] == '\0')) append_text(full_path, "/");
        if (is_dir) {
            append_text(full_path, "NewFolder");
        } else {
            append_text(full_path, "NewFile");
        }
        if (n > 1) {
            char num[8];
            int len = 0;
            int v = n;
            char rev[8];
            while (v > 0 && len < 7) { rev[len++] = (char)('0' + (v % 10)); v /= 10; }
            for (int i = len - 1; i >= 0; i--) {
                num[len - 1 - i] = rev[i];
            }
            num[len] = '\0';
            append_text(full_path, num);
        }
        if (!is_dir) append_text(full_path, ".txt");
        if (fs_find_node(full_path) == -1) {
            if (is_dir) return fs_create_dir(full_path);
            if (fs_create_file(full_path) == 0) return fs_write_file(full_path, "");
            return -1;
        }
    }
    return -1;
}

static void explorer_build_selected_path(char* out, int out_len) {
    if (!out || out_len <= 0) return;
    out[0] = '\0';
    if (exp_selected < 0 || exp_selected >= MAX_FILES || dir_cache[exp_selected].flags == 0) return;
    explorer_build_path(dir_cache[exp_selected].parent_index, out, out_len);
    if (!(out[0] == '/' && out[1] == '\0')) append_text(out, "/");
    append_text(out, dir_cache[exp_selected].name);
}

static void explorer_open_selected(void) {
    if (exp_selected < 0 || exp_selected >= MAX_FILES || dir_cache[exp_selected].flags == 0) return;
    if (dir_cache[exp_selected].flags == FS_NODE_DIR) {
        explorer_open_dir(exp_selected);
        return;
    }
    {
        int pidx = nwm_get_idx_by_type(WIN_TYPE_NARCPAD);
        if (pidx != -1) {
            fs_read_file_by_idx(exp_selected, pad_content, sizeof(pad_content));
            pad_file_idx = exp_selected;
            strcpy(windows[pidx].title, dir_cache[exp_selected].name);
            windows[pidx].visible = 1;
            nwm_bring_to_front(pidx);
            gui_needs_redraw = 1;
        }
    }
}

static void explorer_open_with_selected(void) {
    explorer_open_selected();
}

static int explorer_delete_selected(void) {
    char path[320];
    if (exp_selected < 0 || exp_selected >= MAX_FILES || dir_cache[exp_selected].flags == 0) return -1;
    explorer_build_selected_path(path, sizeof(path));
    if (path[0] == '\0') return -1;
    if (fs_delete_file(path) == 0) {
        exp_selected = -1;
        gui_needs_redraw = 1;
        return 0;
    }
    return -1;
}

static void explorer_begin_rename_selected(void) {
    int i = 0;
    if (exp_selected < 0 || exp_selected >= MAX_FILES || dir_cache[exp_selected].flags == 0) return;
    while (dir_cache[exp_selected].name[i] != '\0' && i < 31) {
        exp_modal_input[i] = dir_cache[exp_selected].name[i];
        i++;
    }
    exp_modal_input[i] = '\0';
    exp_modal_input_len = i;
    exp_modal_mode = 1;
    gui_needs_redraw = 1;
}

static void explorer_begin_delete_selected(void) {
    if (exp_selected < 0 || exp_selected >= MAX_FILES || dir_cache[exp_selected].flags == 0) return;
    exp_modal_mode = 2;
    gui_needs_redraw = 1;
}

void explorer_modal_append_char(char c) {
    if (exp_modal_mode != 1) return;
    if (c == 0 || c == '/' || exp_modal_input_len >= 31) return;
    exp_modal_input[exp_modal_input_len++] = c;
    exp_modal_input[exp_modal_input_len] = '\0';
    gui_needs_redraw = 1;
}

void explorer_modal_backspace() {
    if (exp_modal_mode != 1 || exp_modal_input_len <= 0) return;
    exp_modal_input_len--;
    exp_modal_input[exp_modal_input_len] = '\0';
    gui_needs_redraw = 1;
}

void explorer_modal_submit() {
    if (exp_modal_mode == 1) {
        char path[320];
        if (exp_selected >= 0 && exp_selected < MAX_FILES && dir_cache[exp_selected].flags != 0 && exp_modal_input[0] != '\0') {
            explorer_build_selected_path(path, sizeof(path));
            if (path[0] != '\0') fs_rename(path, exp_modal_input);
        }
        explorer_cancel_modal();
    } else if (exp_modal_mode == 2) {
        explorer_delete_selected();
        explorer_cancel_modal();
    }
}

static int explorer_move_selected_to(int target_dir) {
    char path[320];
    char target_path[256];
    if (exp_drag_idx < 0 || exp_drag_idx >= MAX_FILES || dir_cache[exp_drag_idx].flags == 0) return -1;
    if (target_dir >= 0 && dir_cache[target_dir].flags != FS_NODE_DIR) return -1;
    if (exp_drag_idx == target_dir) return -1;
    explorer_build_path(dir_cache[exp_drag_idx].parent_index, path, sizeof(path));
    if (!(path[0] == '/' && path[1] == '\0')) append_text(path, "/");
    append_text(path, dir_cache[exp_drag_idx].name);
    explorer_build_path(target_dir, target_path, sizeof(target_path));
    if (fs_move_file(path, target_path) == 0) {
        exp_selected = -1;
        gui_needs_redraw = 1;
        return 0;
    }
    return -1;
}

extern void isr_default();
extern void irq0_timer();
extern void irq1_keyboard();
extern void irq12_mouse();

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
    set_idt_gate(8, (uint32_t)isr_double_fault, 0x8E);
    set_idt_gate(13, (uint32_t)isr_gpf, 0x8E);
    set_idt_gate(32, (uint32_t)irq0_timer, 0x8E);
    set_idt_gate(33, (uint32_t)irq1_keyboard, 0x8E);
    set_idt_gate(44, (uint32_t)irq12_mouse, 0x8E);
    asm volatile("lidt %0" : : "m"(idtr));
    asm volatile("sti");
}

void handle_timer() {
    timer_ticks++;
    
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

void gpf_handler(trap_frame_t* frame) {
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
    while(1) asm volatile("hlt");
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

void execute_command(char* cmd) {
    if (cmd[0] == '\0') return;
    char arg1[32] = {0};
    char arg2[128] = {0};
    int i = 0, j = 0;
    while (cmd[i] != ' ' && cmd[i] != '\0' && i < 31) {
        arg1[i] = cmd[i];
        i++;
    }
    arg1[i] = '\0';
    while (cmd[i] == ' ') i++;
    while (cmd[i] != '\0' && j < 127) {
        arg2[j] = cmd[i];
        i++; j++;
    }
    arg2[j] = '\0';
    if (strcmp(arg1, "help") == 0) {
        vga_println("NarcOs Shell");
        vga_println("  help   - Show this menu");
        vga_println("  clear  - Clear the screen");
        vga_println("  mem    - Memory map");
        vga_println("  snake  - Snake game");
        vga_println("  ver    - Show version");
        vga_println("  uptime - Show system uptime in seconds");
        vga_println("  date   - Show current date (RTC)");
        vga_println("  time   - Show current time (RTC)");
        vga_println("  ls     - List files");
        vga_println("  pwd    - Show current path");
        vga_println("  touch  - Create empty file (touch <file>)");
        vga_println("  cat    - Read file (cat <file>)");
        vga_println("  write  - Write to file (write <file> <text>)");
        vga_println("  edit   - Open file in NarcVim (edit <file>)");
        vga_println("  mkdir  - Create directory (mkdir <name>)");
        vga_println("  cd     - Change directory (cd <name> or cd ..)");
        vga_println("  rm     - Delete file (rm <file>)");
        vga_println("  mv     - Move item (mv <src> <target-dir>)");
        vga_println("  ren    - Rename item (ren <path> <new-name>)");
        vga_println("  net    - Show network status");
        vga_println("  dhcp   - Request IPv4 configuration");
        vga_println("  dns    - Resolve hostname to IPv4");
        vga_println("  ping   - Ping an IPv4 host");
        vga_println("  ntp    - Query UTC time from an NTP server");
        vga_println("  http   - Fetch HTTP/1.0 response (http <host> [path])");
        vga_println("  netdemo - Run Ring 3 HTTP demo (netdemo <host> [path])");
        vga_println("  fetch  - Download HTTP body to a file (fetch <host> [path] <file>)");
        vga_println("  malloc_test - Test dynamic heap memory");
        vga_println("  usermode_test - Test Ring 3 transition and syscall");
    } else if (strcmp(arg1, "usermode_test") == 0) {
        vga_println("Launching Secure User Mode Test V12 (Final Victory)...");
        extern void jump_to_usermode_v9(uint32_t eip, uint32_t esp, uint32_t lfb);
        extern void usermode_entry_gate();
        
        uint32_t user_esp = 0x90000; 
        uint32_t lfb_addr = *(uint32_t*)(0x6100 + 40);

        char buf[64];
        uint32_t target_eip = (uint32_t)usermode_entry_gate;
        uint32_t* magic_ptr = (uint32_t*)(target_eip - 4);
        
        vga_print("Target EIP Sym: ");
        vga_print_int_hex(target_eip, buf);
        vga_println(buf);

        if (*magic_ptr != 0xDEADC0DE) {
            vga_println("CRITICAL: MAGIC NUMBER MISMATCH!");
            return;
        }

        vga_println("Magic Recognized. Transitioning to Ring 3...");
        vga_println("Verification: If the heartbeat pixel is rotating and the");
        vga_println("mouse is responsive, the transition was successful!");

        set_tss_stack(0x2800000); 
        
        jump_to_usermode_v9(target_eip, user_esp, lfb_addr);
    } else if (strcmp(arg1, "snake") == 0) {
        open_snake_window();
        vga_println("Snake launched in Ring 3.");
    } else if (strcmp(arg1, "clear") == 0) {
        clear_screen();
    } else if (strcmp(arg1, "mem") == 0) {
        print_memory_info();
    } else if (strcmp(arg1, "malloc_test") == 0) {
        malloc_test();
    } else if (strcmp(arg1, "ver") == 0) {
        vga_println("NarcOs");
    } else if (strcmp(arg1, "uptime") == 0) {
        vga_print("System Uptime (seconds): ");
        vga_print_int(timer_ticks / 100);
        vga_println("");
    } else if (strcmp(arg1, "date") == 0) {
        read_rtc();
        vga_print("Current Date: 20");
        vga_print_int(get_year());
        vga_print("-");
        if (get_month() < 10) vga_print("0");
        vga_print_int(get_month());
        vga_print("-");
        if (get_day() < 10) vga_print("0");
        vga_print_int(get_day());
        vga_println("");
    } else if (strcmp(arg1, "time") == 0) {
        read_rtc();
        vga_print("Current Time: ");
        if (get_hour() < 10) vga_print("0");
        vga_print_int(get_hour());
        vga_print(":");
        if (get_minute() < 10) vga_print("0");
        vga_print_int(get_minute());
        vga_print(":");
        if (get_second() < 10) vga_print("0");
        vga_print_int(get_second());
        vga_println("");
    } else if (strcmp(arg1, "ls") == 0) {
        fs_list_dir();
    } else if (strcmp(arg1, "pwd") == 0) {
        char path[256];
        fs_get_current_path(path, sizeof(path));
        vga_println(path);
    } else if (strcmp(arg1, "touch") == 0) {
        if (arg2[0] == '\0') {
            vga_print_color("Usage: touch <file>\n", 0x0E);
            return;
        }
        if (fs_create_file(arg2) == 0 || fs_find_node(arg2) >= 0) {
            vga_println("Success.");
        } else {
            vga_print_color("error: Failed to create file.\n", 0x0C);
        }
    } else if (strcmp(arg1, "cat") == 0) {
        if (arg2[0] == '\0') {
            vga_print_color("Usage: cat <file>\n", 0x0E);
            return;
        }
        char buffer[2048] = {0};
        if (fs_read_file(arg2, buffer, sizeof(buffer)) == 0) {
            vga_println(buffer);
        } else {
            vga_print_color("error: File not found.\n", 0x0C);
        }
    } else if (strcmp(arg1, "write") == 0) {
        if (arg2[0] == '\0') {
            vga_print_color("Usage: write <file> <text>\n", 0x0E);
            return;
        }
        char file_name[32] = {0};
        int k = 0;
        while(arg2[k] != ' ' && arg2[k] != '\0' && k < 31) {
            file_name[k] = arg2[k];
            k++;
        }
        file_name[k] = '\0';
        while (arg2[k] == ' ') k++;
        char* file_content = &arg2[k];
        if (file_content[0] == '\0') {
            vga_print_color("error: Text cannot be empty.\n", 0x0C);
            return;
        }
        if (fs_write_file(file_name, file_content) == 0) {
            vga_println("Success.");
        } else {
            vga_print_color("error: Failed to create file or not enough space.\n", 0x0C);
        }
    } else if (strcmp(arg1, "edit") == 0) {
        if (arg2[0] == '\0') {
            vga_print_color("Usage: edit <file>\n", 0x0E);
            return;
        }
        editor_start(arg2);
        clear_screen();
        vga_print_color("Exited NarcVim. ", 0x0A);
        vga_println(arg2);
    } else if (strcmp(arg1, "mkdir") == 0) {
        if (arg2[0] == '\0') {
            vga_print_color("Usage: mkdir <name>\n", 0x0E);
            return;
        }
        if (fs_create_dir(arg2) == 0) {
            vga_println("Directory created.");
        } else {
            vga_print_color("error: Failed to create directory.\n", 0x0C);
        }
    } else if (strcmp(arg1, "cd") == 0) {
        if (arg2[0] == '\0') {
            vga_print_color("Usage: cd <name>\n", 0x0E);
            return;
        }
        if (fs_change_dir(arg2) != 0) {
            vga_print_color("error: Directory not found.\n", 0x0C);
        }
    } else if (strcmp(arg1, "rm") == 0) {
        if (arg2[0] == '\0') {
            vga_print_color("Usage: rm <file>\n", 0x0E);
            return;
        }
        if (fs_delete_file(arg2) == 0) {
            vga_println("Success. Item deleted.");
        } else {
            vga_print_color("error: Item not found or directory not empty.\n", 0x0C);
        }
    } else if (strcmp(arg1, "mv") == 0) {
        char src[64] = {0};
        int k = 0;
        while (arg2[k] != ' ' && arg2[k] != '\0' && k < 63) {
            src[k] = arg2[k];
            k++;
        }
        src[k] = '\0';
        while (arg2[k] == ' ') k++;
        if (src[0] == '\0' || arg2[k] == '\0') {
            vga_print_color("Usage: mv <src> <target-dir>\n", 0x0E);
            return;
        }
        if (fs_move_file(src, &arg2[k]) == 0) {
            vga_println("Success.");
        } else {
            vga_print_color("error: Move failed.\n", 0x0C);
        }
    } else if (strcmp(arg1, "ren") == 0) {
        char src[64] = {0};
        int k = 0;
        while (arg2[k] != ' ' && arg2[k] != '\0' && k < 63) {
            src[k] = arg2[k];
            k++;
        }
        src[k] = '\0';
        while (arg2[k] == ' ') k++;
        if (src[0] == '\0' || arg2[k] == '\0') {
            vga_print_color("Usage: ren <path> <new-name>\n", 0x0E);
            return;
        }
        if (fs_rename(src, &arg2[k]) == 0) {
            vga_println("Success.");
        } else {
            vga_print_color("error: Rename failed.\n", 0x0C);
        }
    } else if (strcmp(arg1, "net") == 0) {
        net_print_status();
    } else if (strcmp(arg1, "dhcp") == 0) {
        (void)net_run_dhcp(1);
    } else if (strcmp(arg1, "dns") == 0) {
        (void)net_dns_command(arg2);
    } else if (strcmp(arg1, "ping") == 0) {
        (void)net_ping_command(arg2);
    } else if (strcmp(arg1, "ntp") == 0) {
        (void)net_ntp_command(arg2);
    } else if (strcmp(arg1, "http") == 0) {
        (void)net_http_command(arg2);
    } else if (strcmp(arg1, "netdemo") == 0) {
        (void)run_user_netdemo(arg2);
    } else if (strcmp(arg1, "fetch") == 0) {
        (void)run_user_fetch(arg2);
    } else {
        vga_print_color("Error: Unknown command '", 0x0C);
        vga_print_color(arg1, 0x0C);
        vga_println("'");
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
    exp_dir = desk_dir_idx;
    exp_prev_dir = -1;
    exp_selected = -1;

    uint32_t last_clock_tick = 0;
    uint32_t last_click_tick = 0;
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
                        if (dir_cache[i].flags != 0 && dir_cache[i].parent_index == exp_dir) {
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
                         int pidx = nwm_get_idx_by_type(WIN_TYPE_NARCPAD);
                         if (pidx != -1) {
                             if (pad_file_idx >= 0) fs_write_file_by_idx(pad_file_idx, pad_content);
                             else fs_write_file(windows[pidx].title, pad_content);
                         }
                     } else if (strcmp(cmd, "Open") == 0) {
                         explorer_open_selected();
                     } else if (strcmp(cmd, "Open With") == 0) {
                         explorer_open_with_selected();
                     } else if (strcmp(cmd, "Rename") == 0) {
                         explorer_begin_rename_selected();
                     } else if (strcmp(cmd, "Delete") == 0) {
                         explorer_begin_delete_selected();
                     } else if (strcmp(cmd, "New File") == 0) {
                         explorer_create_in_dir(exp_dir, 0);
                     } else if (strcmp(cmd, "New Folder") == 0) {
                         explorer_create_in_dir(exp_dir, 1);
                     } else if (strcmp(cmd, "Close") == 0) {
                         int pidx = nwm_get_idx_by_type(WIN_TYPE_NARCPAD);
                         if (pidx != -1) windows[pidx].visible = 0;
                         pad_file_idx = -1;
                     } else if (strcmp(cmd, "Refresh") == 0) {
                         gui_needs_redraw = 1;
                     }
                 }
                 ctx_visible = 0; gui_needs_redraw = 1;
                 goto process_done;
            }
            int double_click = (timer_ticks - last_click_tick < 40);
            last_click_tick = timer_ticks;
            if (my <= 35) {
                if (mx >= 5 && mx <= 89) { start_menu_visible = !start_menu_visible; gui_needs_redraw = 1; }
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
                                if (exp_prev_dir != -1) explorer_open_dir(exp_prev_dir);
                            } else if (mx >= content_x + 66 && mx <= content_x + 104) {
                                if (exp_dir != -1) explorer_open_dir(dir_cache[exp_dir].parent_index);
                            } else if (mx >= content_x + 112 && mx <= content_x + 182) {
                                if (explorer_create_in_dir(exp_dir, 0) == 0) gui_needs_redraw = 1;
                            } else if (mx >= content_x + 190 && mx <= content_x + 274) {
                                if (explorer_create_in_dir(exp_dir, 1) == 0) gui_needs_redraw = 1;
                            } else if (mx >= content_x + 282 && mx <= content_x + 342) {
                                explorer_begin_rename_selected();
                            } else if (mx >= content_x + 350 && mx <= content_x + 404) {
                                explorer_begin_delete_selected();
                            } else if (mx >= content_x + content_w - 76 && mx <= content_x + content_w - 16) {
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
                                    if (dir_cache[i].flags != 0 && dir_cache[i].parent_index == exp_dir) {
                                        if (current_slot == hit_slot) {
                                            exp_selected = i;
                                            exp_drag_idx = i;
                                            exp_drag_source_dir = exp_dir;
                                            exp_drag_armed = 1;
                                            if (dir_cache[i].flags == 2) {
                                                explorer_open_dir(i);
                                            } else {
                                                int pidx = nwm_get_idx_by_type(WIN_TYPE_NARCPAD);
                                                if (pidx != -1) {
                                                    fs_read_file_by_idx(i, pad_content, sizeof(pad_content));
                                                    pad_file_idx = i;
                                                    strcpy(windows[pidx].title, dir_cache[i].name);
                                                    windows[pidx].visible = 1; nwm_bring_to_front(pidx); gui_needs_redraw = 1;
                                                }
                                            }
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
                                exp_selected = -1;
                                for (int i = 0; i < MAX_FILES; i++) {
                                    if (dir_cache[i].flags != 0 && dir_cache[i].parent_index == exp_dir) {
                                        if (current_slot == hit_slot) {
                                            exp_selected = i;
                                            exp_drag_idx = i;
                                            exp_drag_source_dir = exp_dir;
                                            exp_drag_armed = 1;
                                            gui_needs_redraw = 1;
                                            break;
                                        }
                                        current_slot++;
                                    }
                                }
                            }
                        }
                    }
                } else {
                    if (start_menu_visible && (mx > 200 || my > 335)) { start_menu_visible = 0; gui_needs_redraw = 1; }
                    if (mx >= 20 && mx <= 60) {
                        if (my >= 60 && my <= 110 && double_click) { 
                            int idx = nwm_get_idx_by_type(WIN_TYPE_EXPLORER);
                            if (idx != -1) { windows[idx].visible = 1; nwm_bring_to_front(idx); gui_needs_redraw = 1; }
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
                                            int idx = nwm_get_idx_by_type(WIN_TYPE_EXPLORER);
                                            if (idx != -1) { explorer_open_dir(i); windows[idx].visible = 1; nwm_bring_to_front(idx); }
                                        } else {
                                            int idx = nwm_get_idx_by_type(WIN_TYPE_NARCPAD);
                                            if (idx != -1) {
                                                fs_read_file_by_idx(i, pad_content, sizeof(pad_content));
                                                pad_file_idx = i;
                                                strcpy(windows[idx].title, dir_cache[i].name);
                                                windows[idx].visible = 1; nwm_bring_to_front(idx); gui_needs_redraw = 1;
                                            }
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
        net_poll();
        run_user_tasks();
        asm volatile("hlt");
    }
}

void kmain() {
    init_pic();
    init_pit();
    init_gdt();
    load_idt();
    init_syscalls();
    init_usermode();
    init_keyboard();
    init_fs();
    init_heap();
    init_vbe();
    init_mouse();
    net_init();
    clear_screen();
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
    desktop_process_main();
}
