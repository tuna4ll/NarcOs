#include <stdint.h>
#include "string.h"
#include "fs.h"
#include "rtc.h"
#include "editor.h"
#include "memory_alloc.h"
#include "vbe.h"
#include "mouse.h"

extern void outb(uint16_t port, uint8_t val);
extern void clear_screen();
extern void vga_print(const char* str);
extern void vga_print_color(const char* str, uint8_t color);
extern void vga_println(const char* str);
extern void vga_print_int(int num);
extern void vga_set_window_pos(int x, int y);
extern int vga_get_window_x();
extern int vga_get_window_y();
extern int vga_get_window_w();
extern int vga_get_window_h();
extern int vga_get_title_h();
extern void vga_refresh_window();
extern void* vga_get_window_buffer();

extern void init_keyboard();

extern void print_memory_info();

extern void snake_main();
extern int snake_running;

typedef struct {
    uint16_t isr_low;
    uint16_t kernel_cs;
    uint8_t  reserved;
    uint8_t  attributes;
    uint16_t isr_high;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idtr_t;

idt_entry_t idt[256];
idtr_t idtr;

volatile uint32_t timer_ticks = 0;

extern void isr_default();
extern void irq0_timer();
extern void irq1_keyboard();
extern void irq12_mouse();

void set_idt_gate(int n, uint32_t handler) {
    idt[n].isr_low = (uint16_t)(handler & 0xFFFF);
    idt[n].kernel_cs = 0x08;
    idt[n].reserved = 0;
    idt[n].attributes = 0x8E;
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

void load_idt() {
    idtr.base = (uint32_t)&idt;
    idtr.limit = 256 * sizeof(idt_entry_t) - 1;
    for (int i = 0; i < 256; i++) {
        set_idt_gate(i, (uint32_t)isr_default);
    }
    set_idt_gate(32, (uint32_t)irq0_timer);
    set_idt_gate(33, (uint32_t)irq1_keyboard);
    set_idt_gate(44, (uint32_t)irq12_mouse);
    asm volatile("lidt %0" : : "m"(idtr));
    asm volatile("sti");
}

void handle_timer() {
    timer_ticks++;
    outb(0x20, 0x20);
}

void isr_handler_default() {
    outb(0x20, 0x20);
    outb(0xA0, 0x20);
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
        vga_println("  cat    - Read file (cat <file>)");
        vga_println("  write  - Write to file (write <file> <text>)");
        vga_println("  edit   - Open file in NarcVim (edit <file>)");
        vga_println("  mkdir  - Create directory (mkdir <name>)");
        vga_println("  cd     - Change directory (cd <name> or cd ..)");
        vga_println("  rm     - Delete file (rm <file>)");
        vga_println("  malloc_test - Test dynamic heap memory");
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
    } else if (strcmp(arg1, "snake") == 0) {
        snake_main();
        clear_screen();
        vga_print_color("Bad score I guess...\n", 0x0C);
    } else if (strcmp(arg1, "ls") == 0) {
        fs_list_dir();
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
            vga_print_color("hata: Metin bos olamaz.\n", 0x0C);
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
            vga_println("Success. File deleted.");
        } else {
            vga_print_color("error: File not found.\n", 0x0C);
        }
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
    vga_print_color("/", 0x0B);
    char dname[32];
    get_current_dir_name(dname);
    if (dname[0] != '\0') {
        vga_print_color(dname, 0x0B);
    }
    vga_print_color("$ ", 0x0A);
}

void kmain() {
    init_pic();
    init_pit();
    load_idt();
    init_keyboard();
    init_fs();
    init_heap();
    init_vbe();
    init_mouse();
    clear_screen();
    vga_print_color("\n  NarcOs GUI Initialized.\n", 0x0B);
    vga_print_color("  ========================\n", 0x0B);
    vga_println("  Welcome to NarcOs Desktop!");
    print_prompt();
    vga_print("  Video Mode: ");
    vga_print_int(vbe_get_width());
    vga_print("x");
    vga_print_int(vbe_get_height());
    vga_print(" @ ");
    vga_print_int(vbe_get_bpp());
    vga_println("bpp");
    int last_mx = -1, last_my = -1;
    int last_lp = -1;
    int last_wx = -1, last_wy = -1;
    int dragging = 0, exp_dragging = 0;
    int drag_off_x = 0, drag_off_y = 0;
    extern int win_visible;
    int start_menu_visible = 0;
    int exp_visible = 0, exp_x = 200, exp_y = 150, exp_cur_dir = -1;
    uint32_t last_clock_tick = 0;
    uint32_t last_click_tick = 0;
    int mx = get_mouse_x();
    int my = get_mouse_y();
    int lp = mouse_left_pressed();
    int wx = vga_get_window_x();
    int wy = vga_get_window_y();
    vga_refresh_window();
    vbe_compose_scene(wx, wy, win_visible, start_menu_visible, exp_visible, exp_x, exp_y, exp_cur_dir); 
    while (1) {
        mx = get_mouse_x();
        my = get_mouse_y();
        lp = mouse_left_pressed();
        wx = vga_get_window_x();
        wy = vga_get_window_y();
        if (lp != last_lp && lp) {
            int double_click = (timer_ticks - last_click_tick < 40);
            last_click_tick = timer_ticks;
            if (my <= 35) {
                if (mx >= 5 && mx <= 75) {
                    start_menu_visible = !start_menu_visible;
                    gui_needs_redraw = 1;
                } else if (mx >= 80 && mx <= 170) {
                    win_visible = !win_visible;
                    start_menu_visible = 0;
                    gui_needs_redraw = 1;
                }
            } else if (exp_visible && mx >= exp_x && mx <= exp_x + 600 && my >= exp_y && my <= exp_y + 400) {
                if (my <= exp_y + 25) {
                    if (mx >= exp_x + 600 - 25) { exp_visible = 0; gui_needs_redraw = 1; }
                    else { exp_dragging = 1; drag_off_x = mx - exp_x; drag_off_y = my - exp_y; }
                }
            } else if (win_visible && mx >= wx && mx <= wx + vga_get_window_w() && my >= wy && my <= wy + 475) {
                if (my <= wy + vga_get_title_h()) {
                    if (mx >= wx + vga_get_window_w() - 25) { win_visible = 0; gui_needs_redraw = 1; }
                    else { dragging = 1; drag_off_x = mx - wx; drag_off_y = my - wy; }
                }
            } else if (start_menu_visible) {
                if (mx > 205 || my > 335) { start_menu_visible = 0; gui_needs_redraw = 1; }
            } else {
                if (mx >= 20 && mx <= 60) {
                    if (my >= 60 && my <= 110 && double_click) { exp_visible = 1; gui_needs_redraw = 1; }
                    else if (my >= 140 && my <= 190 && double_click) { win_visible = 1; gui_needs_redraw = 1; }
                }
            }
        } else if (!lp) {
            dragging = 0; exp_dragging = 0;
        }
        if (dragging && win_visible) {
            int new_wx = mx - drag_off_x, new_wy = my - drag_off_y;
            if (new_wx < 0) new_wx = 0; 
            if (new_wy < 35) new_wy = 35;
            vga_set_window_pos(new_wx, new_wy); wx = new_wx; wy = new_wy; gui_needs_redraw = 1;
        }
        if (exp_dragging && exp_visible) {
            exp_x = mx - drag_off_x; exp_y = my - drag_off_y;
            if (exp_x < 0) exp_x = 0; 
            if (exp_y < 35) exp_y = 35;
            gui_needs_redraw = 1;
        }
        if (timer_ticks - last_clock_tick >= 100) {
            read_rtc(); last_clock_tick = timer_ticks; gui_needs_redraw = 1;
        }
        if (mx != last_mx || my != last_my || lp != last_lp || wx != last_wx || wy != last_wy || gui_needs_redraw || cmd_ready) {
            vbe_compose_scene(wx, wy, win_visible, start_menu_visible, exp_visible, exp_x, exp_y, exp_cur_dir);
            vbe_prepare_frame_from_composition();
            vbe_render_mouse(mx, my);
            wait_vsync();
            vbe_update();
            last_mx = mx; last_my = my; last_lp = lp; last_wx = wx; last_wy = wy;
            gui_needs_redraw = 0;
        }
        if (cmd_ready) {
            execute_command(cmd_to_execute); cmd_ready = 0; print_prompt();
        }
        asm volatile("hlt");
    }
}
