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
extern void init_keyboard();
extern disk_fs_node_t dir_cache[MAX_FILES];

extern void print_memory_info();

extern void print_memory_info();

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

window_t windows[MAX_WINDOWS];
int window_count = 0;
int active_window_idx = -1;

char pad_title[32] = "NarcPad";
char pad_content[1024] = {0};
int pad_file_idx = -1;
volatile int snk_next_dir = -1;
int exp_dir = -1;

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
    windows[1].w = 600; windows[1].h = 400;
    strcpy(windows[1].title, "NarcExplorer");
    windows[1].visible = 0;
    windows[1].minimized = 0;
    windows[1].id = 1;
    windows[2].type = WIN_TYPE_NARCPAD;
    windows[2].x = 200; windows[2].y = 150;
    windows[2].w = 500; windows[2].h = 400;
    strcpy(windows[2].title, "NarcPad");
    windows[2].visible = 0;
    windows[2].minimized = 0;
    windows[2].id = 2;
    windows[3].type = WIN_TYPE_SNAKE;
    windows[3].x = 300; windows[3].y = 200;
    windows[3].w = 400; windows[3].h = 325;
    strcpy(windows[3].title, "NarcSnake");
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
    nwm_init_windows();

    int start_menu_visible = 0;
    int desk_dir_idx = -1;
    int ctx_visible = 0, ctx_x = 0, ctx_y = 0;
    const char* ctx_items_desk[] = {"New File", "New Folder", "Refresh"};
    const char* ctx_items_exp[]  = {"New File", "New Folder", "Delete", "Refresh"};
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
    while (1) {
        mx = get_mouse_x();
        my = get_mouse_y();
        lp = mouse_left_pressed();
        int rp = mouse_right_pressed();
        if (rp != last_rp && rp) {
            ctx_visible = 1; ctx_x = mx; ctx_y = my; ctx_selected = -1;
            int pidx = nwm_get_idx_by_type(WIN_TYPE_NARCPAD);
            int eidx = nwm_get_idx_by_type(WIN_TYPE_EXPLORER);
            if (pidx != -1 && windows[pidx].visible && mx >= windows[pidx].x && mx <= windows[pidx].x + windows[pidx].w && my >= windows[pidx].y && my <= windows[pidx].y + windows[pidx].h) {
                ctx_items = ctx_items_pad; ctx_count = 2;
            } else if (eidx != -1 && windows[eidx].visible && mx >= windows[eidx].x && mx <= windows[eidx].x + windows[eidx].w && my >= windows[eidx].y && my <= windows[eidx].y + windows[eidx].h) {
                ctx_items = ctx_items_exp; ctx_count = 4;
            } else {
                ctx_items = ctx_items_desk; ctx_count = 3;
            }
            gui_needs_redraw = 1;
        }
        if (lp != last_lp && lp) {
            if (ctx_visible) {
                 if (ctx_selected != -1) {
                     const char* cmd = ctx_items[ctx_selected];
                     if (strcmp(cmd, "Save") == 0) {
                         int pidx = nwm_get_idx_by_type(WIN_TYPE_NARCPAD);
                         if (pidx != -1) {
                             fs_write_file(windows[pidx].title, pad_content);
                         }
                     } else if (strcmp(cmd, "Close") == 0) {
                         int pidx = nwm_get_idx_by_type(WIN_TYPE_NARCPAD);
                         if (pidx != -1) windows[pidx].visible = 0;
                     }
                 }
                 ctx_visible = 0; gui_needs_redraw = 1;
                 goto process_done;
            }
            int double_click = (timer_ticks - last_click_tick < 40);
            last_click_tick = timer_ticks;
            if (my <= 35) {
                if (mx >= 5 && mx <= 75) { start_menu_visible = !start_menu_visible; gui_needs_redraw = 1; }
                else if (mx >= 85 && mx <= 125) {
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
                    int clicked_slot = (mx - 135) / 115;
                    if (clicked_slot >= 0) {
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
                    
                    if (my <= windows[hit_win].y + 28) {
                        if (mx >= windows[hit_win].x + windows[hit_win].w - 24) {
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
                        if (my >= wy + 28 && my <= wy + 28 + 24) {
                            if (mx >= wx + 5 && mx <= wx + ww - 5) {
                                exp_dir = -1; gui_needs_redraw = 1;
                            }
                        }
                        else if (my >= wy + 45 && my <= wy + windows[hit_win].h - 10) {
                            int local_x = mx - (wx + 24), local_y = my - (wy + 45);
                            int icons_per_row = (ww - 40) / 90;
                            if (local_x >= 0 && local_y >= 0) {
                                int col = local_x / 90;
                                int row = local_y / 85;
                                int hit_slot = row * icons_per_row + col;
                                
                                int current_slot = 0;
                                for (int i = 0; i < MAX_FILES; i++) {
                                    if (dir_cache[i].flags != 0 && dir_cache[i].parent_index == exp_dir) {
                                        if (current_slot == hit_slot && double_click) {
                                            if (dir_cache[i].flags == 2) {
                                                exp_dir = i; gui_needs_redraw = 1;
                                            } else {
                                                int pidx = nwm_get_idx_by_type(WIN_TYPE_NARCPAD);
                                                if (pidx != -1) {
                                                    fs_read_file(dir_cache[i].name, pad_content, sizeof(pad_content));
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
                            int idx = nwm_get_idx_by_type(WIN_TYPE_SNAKE);
                            if (idx != -1) { windows[idx].visible = 1; nwm_bring_to_front(idx); gui_needs_redraw = 1; }
                        }
                        else if (my >= 140 && double_click) {
                            int row_idx = (my - 140) / 80;
                            int current_row = 0;
                            for (int i = 0; i < MAX_FILES; i++) {
                                if (dir_cache[i].flags != 0 && dir_cache[i].parent_index == desk_dir_idx) {
                                    if (current_row == row_idx) {
                                        if (dir_cache[i].flags == 2) {
                                            int idx = nwm_get_idx_by_type(WIN_TYPE_EXPLORER);
                                            if (idx != -1) { exp_dir = i; windows[idx].visible = 1; nwm_bring_to_front(idx); gui_needs_redraw = 1; }
                                        } else {
                                            int idx = nwm_get_idx_by_type(WIN_TYPE_NARCPAD);
                                            if (idx != -1) {
                                                fs_read_file(dir_cache[i].name, pad_content, sizeof(pad_content));
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
            drag_file_idx = -1;
        }

        if (dragging_idx != -1) {
            uint32_t sw = vbe_get_width();
            uint32_t sh = vbe_get_height();
            int win_w = windows[dragging_idx].w;
            int win_h = windows[dragging_idx].h;

            windows[dragging_idx].x = mx - drag_off_x;
            windows[dragging_idx].y = my - drag_off_y;

            if (windows[dragging_idx].y < 35) windows[dragging_idx].y = 35;
            if (windows[dragging_idx].y > (int)sh - 20) windows[dragging_idx].y = (int)sh - 20;
            if (windows[dragging_idx].x < -(win_w - 40)) windows[dragging_idx].x = -(win_w - 40);
            if (windows[dragging_idx].x > (int)sw - 40) windows[dragging_idx].x = (int)sw - 40;

            gui_needs_redraw = 1;
        }
        
        process_done: (void)0;
        int sidx = nwm_get_idx_by_type(WIN_TYPE_SNAKE);
        if (sidx != -1 && windows[sidx].visible && !snk_dead && (timer_ticks - last_snk_tick > 10)) {
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
        if (mx != last_mx || my != last_my || lp != last_lp || rp != last_rp || gui_needs_redraw || cmd_ready) {
            vbe_compose_scene(windows, window_count, active_window_idx, start_menu_visible, desk_dir_idx, drag_file_idx, mx, my, ctx_visible, ctx_x, ctx_y, ctx_items, ctx_count, ctx_selected);
            vbe_prepare_frame_from_composition();
            vbe_render_mouse(mx, my);
            wait_vsync();
            vbe_update();
            last_mx = mx; last_my = my; last_lp = lp; last_rp = rp;
            gui_needs_redraw = 0;
        }
        if (ctx_visible) {
            int new_sel = -1;
            if (mx >= ctx_x && mx <= ctx_x + 120 && my >= ctx_y && my <= ctx_y + ctx_count * 20) {
                new_sel = (my - ctx_y) / 20;
            }
            if (new_sel != ctx_selected) {
                ctx_selected = new_sel;
                gui_needs_redraw = 1;
            }
        }

        if (cmd_ready) {
            execute_command(cmd_to_execute); cmd_ready = 0; print_prompt();
        }
        asm volatile("hlt");
    }
}
