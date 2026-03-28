#include <stdint.h>
#include <stddef.h>
#include "fs.h"
extern void    outb(uint16_t port, uint8_t val);
extern uint8_t inb(uint16_t port);
extern void    vga_putchar(char c);
extern void    vga_backspace();
extern void    vga_newline();
#define INPUT_BUF_SIZE 128
char input_buf[INPUT_BUF_SIZE];
int  input_pos  = 0;
volatile int cmd_ready = 0;
char cmd_to_execute[INPUT_BUF_SIZE];
#define HISTORY_MAX 10
char history[HISTORY_MAX][INPUT_BUF_SIZE];
int history_count = 0;
int history_current_idx = -1; 
int history_write_idx = 0;    
int lctrl_pressed = 0;
int rctrl_pressed = 0;
int lshift_pressed  = 0;
int rshift_pressed  = 0;
int capslock_active = 0;
const char scancode_map[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8',
    '9', '0', '-', '=', '\b','\t','q', 'w', 'e', 'r',
    't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
    '\'','`', 0,  '\\','z', 'x', 'c', 'v', 'b', 'n',
    'm', ',', '.', '/', 0,  '*', 0,  ' ', 0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   '7', '8', '9', '-', '4', '5', '6', '+',
    '1', '2', '3', '0', '.', 0,   0,   0,   0,   0
};
const char scancode_map_shift[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*',
    '(', ')', '_', '+', '\b','\t','Q', 'W', 'E', 'R',
    'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"', '~', 0,  '|', 'Z', 'X', 'C', 'V', 'B', 'N',
    'M', '<', '>', '?', 0,  '*', 0,  ' ', 0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   '7', '8', '9', '-', '4', '5', '6', '+',
    '1', '2', '3', '0', '.', 0,   0,   0,   0,   0
};
void init_keyboard()
{
    input_pos = 0;
    for (int i = 0; i < INPUT_BUF_SIZE; i++) input_buf[i] = 0;
}
#include "vbe.h"
#include "usermode.h"
extern window_t windows[MAX_WINDOWS];
extern int window_count;
extern int active_window_idx;

extern volatile int snk_next_dir;
extern volatile int editor_running;
extern volatile int editor_input_key;
extern volatile int editor_special_key;
extern char pad_content[1024];
extern int pad_file_idx;
extern volatile int gui_needs_redraw;
extern int explorer_modal_active();
extern void explorer_cancel_modal();
extern void explorer_modal_submit();
extern void explorer_modal_backspace();
extern void explorer_modal_append_char(char c);

static const char* shell_commands[] = {
    "help", "clear", "mem", "snake", "ver", "uptime", "date", "time",
    "ls", "pwd", "touch", "cat", "write", "edit", "mkdir", "cd",
    "rm", "mv", "ren", "net", "dhcp", "dns", "ping", "ntp", "http", "netdemo", "fetch",
    "malloc_test", "usermode_test", "reboot", "poweroff", "hwinfo", "pci", "storage", "log"
};

static void set_input_line(const char* text) {
    int i = 0;
    while (input_pos > 0) {
        input_pos--;
        vga_backspace();
    }
    while (text[i] != '\0' && i < INPUT_BUF_SIZE - 1) {
        input_buf[i] = text[i];
        vga_putchar(text[i]);
        i++;
    }
    input_pos = i;
    input_buf[input_pos] = '\0';
}

static void autocomplete_input_line() {
    int prefix_len = 0;
    int matches = 0;
    int match_idx = -1;
    for (int i = 0; i < input_pos; i++) {
        if (input_buf[i] == ' ') return;
    }
    prefix_len = input_pos;
    if (prefix_len == 0) return;
    for (int i = 0; i < (int)(sizeof(shell_commands) / sizeof(shell_commands[0])); i++) {
        int ok = 1;
        for (int j = 0; j < prefix_len; j++) {
            if (shell_commands[i][j] != input_buf[j]) {
                ok = 0;
                break;
            }
        }
        if (ok) {
            matches++;
            match_idx = i;
        }
    }
    if (matches == 1 && match_idx >= 0) {
        char completed[INPUT_BUF_SIZE];
        int i = 0;
        while (shell_commands[match_idx][i] != '\0' && i < INPUT_BUF_SIZE - 2) {
            completed[i] = shell_commands[match_idx][i];
            i++;
        }
        completed[i++] = ' ';
        completed[i] = '\0';
        set_input_line(completed);
    }
}

void handle_keyboard()
{
    uint8_t status = inb(0x64);
    if (!(status & 0x01) || (status & 0x20)) {
        if (status & 0x01) {
            // Mouse data is in buffer, but this is the keyboard ISR.
            // Let the mouse ISR handle it.
        }
        outb(0x20, 0x20);
        return;
    }
    uint8_t scancode = inb(0x60);
    if (scancode & 0x80) {
        uint8_t key = scancode & 0x7F;
        if (key == 0x1D) lctrl_pressed = 0;
        if      (key == 0x2A) lshift_pressed = 0;
        else if (key == 0x36) rshift_pressed = 0;
        outb(0x20, 0x20);
        return;
    }
    if (scancode == 0x1D) {
        lctrl_pressed = 1;
        outb(0x20, 0x20);
        return;
    }
    if (scancode == 0x2A) {
        lshift_pressed = 1;
        outb(0x20, 0x20);
        return;
    }
    if (scancode == 0x36) {
        rshift_pressed = 1;
        outb(0x20, 0x20);
        return;
    }
    if (scancode == 0x3A) {
        capslock_active = !capslock_active;
        outb(0x20, 0x20);
        return;
    }
    int is_shift = lshift_pressed || rshift_pressed;
    int is_ctrl = lctrl_pressed || rctrl_pressed;
    if (explorer_modal_active()) {
        char c = is_shift ? scancode_map_shift[scancode] : scancode_map[scancode];
        if (capslock_active && c >= 'a' && c <= 'z') c -= 32;
        else if (capslock_active && c >= 'A' && c <= 'Z') c += 32;
        if (scancode == 0x0E) explorer_modal_backspace();
        else if (scancode == 0x1C) explorer_modal_submit();
        else if (scancode == 0x01) explorer_cancel_modal();
        else if (c != 0) explorer_modal_append_char(c);
        outb(0x20, 0x20);
        return;
    }
    if (active_window_idx != -1 && windows[active_window_idx].visible && windows[active_window_idx].type == WIN_TYPE_SNAKE) {
        switch (scancode) {
            case 0x11: case 0x48:
                if (user_snake_running()) queue_user_snake_input(0);
                else snk_next_dir = 0;
                break;
            case 0x1F: case 0x50:
                if (user_snake_running()) queue_user_snake_input(1);
                else snk_next_dir = 1;
                break;
            case 0x1E: case 0x4B:
                if (user_snake_running()) queue_user_snake_input(2);
                else snk_next_dir = 2;
                break;
            case 0x20: case 0x4D:
                if (user_snake_running()) queue_user_snake_input(3);
                else snk_next_dir = 3;
                break;
            case 0x13: case 0x19:
                if (user_snake_running()) queue_user_snake_input(5);
                else snk_next_dir = 5;
                break;
            case 0x10: case 0x01:
                if (user_snake_running()) queue_user_snake_input(6);
                else snk_next_dir = 6;
                break;
            default: break;
        }
        outb(0x20, 0x20); return;
    }
    if (active_window_idx != -1 && windows[active_window_idx].visible && windows[active_window_idx].type == WIN_TYPE_NARCPAD) {
        char c = is_shift ? scancode_map_shift[scancode] : scancode_map[scancode];
        if (capslock_active && c >= 'a' && c <= 'z') c -= 32;
        else if (capslock_active && c >= 'A' && c <= 'Z') c += 32;

        size_t len = 0;
        while(pad_content[len]) len++;

        if (is_ctrl && scancode == 0x1F) {
            if (pad_file_idx >= 0) fs_write_file_by_idx(pad_file_idx, pad_content);
            else fs_write_file(windows[active_window_idx].title, pad_content);
        } else if (scancode == 0x0E) {
            if (len > 0) pad_content[len-1] = '\0';
        } else if (scancode == 0x1C) {
            if (len < 1022) { pad_content[len] = '\n'; pad_content[len+1] = '\0'; }
        } else if (c != 0 && len < 1023) {
            pad_content[len] = c;
            pad_content[len+1] = '\0';
        }
        gui_needs_redraw = 1;
        outb(0x20, 0x20);
        return;
    }
    if (editor_running) {
        char c = is_shift ? scancode_map_shift[scancode] : scancode_map[scancode];
        switch (scancode) {
            case 0x48: editor_special_key = 1; break;
            case 0x50: editor_special_key = 2; break;
            case 0x4B: editor_special_key = 3; break;
            case 0x4D: editor_special_key = 4; break;
            case 0x0E: editor_special_key = 5; break;
            case 0x1C: editor_special_key = 6; break;
            case 0x1F: if (lshift_pressed == 0 && rshift_pressed == 0 && c != 'S') editor_special_key = 7; else editor_input_key = c; break;
            case 0x3C: editor_special_key = 7; break;
            case 0x01: editor_special_key = 8; break;
            default:
                if (c != 0) {
                    if (capslock_active && c >= 'a' && c <= 'z') c -= 32;
                    else if (capslock_active && c >= 'A' && c <= 'Z') c += 32;
                    editor_input_key = c;
                }
                break;
        }
        outb(0x20, 0x20);
        return;
    }
    if (scancode == 0x0E) {
        if (input_pos > 0) {
            input_pos--;
            input_buf[input_pos] = '\0';
            vga_backspace();
        }
    }
    else if (scancode == 0x1C) {
        vga_newline();
        input_buf[input_pos] = '\0';
        if (input_pos > 0) {
            int last_idx = (history_write_idx + HISTORY_MAX - 1) % HISTORY_MAX;
            int is_same = 0;
            if (history_count > 0) {
                int k = 0;
                is_same = 1;
                while (input_buf[k] != '\0' || history[last_idx][k] != '\0') {
                    if (input_buf[k] != history[last_idx][k]) {
                        is_same = 0;
                        break;
                    }
                    k++;
                }
            }
            if (!is_same) {
                int k = 0;
                while (input_buf[k] != '\0' && k < INPUT_BUF_SIZE - 1) {
                    history[history_write_idx][k] = input_buf[k];
                    k++;
                }
                history[history_write_idx][k] = '\0';
                history_write_idx = (history_write_idx + 1) % HISTORY_MAX;
                if (history_count < HISTORY_MAX) history_count++;
            }
        }
        history_current_idx = -1; 
        for (int i = 0; i < INPUT_BUF_SIZE; i++)
            cmd_to_execute[i] = input_buf[i];
        cmd_ready = 1;
        input_pos = 0;
        for (int i = 0; i < INPUT_BUF_SIZE; i++) input_buf[i] = 0;
    }
    else if (scancode == 0x48) { 
        if (history_count > 0) {
            if (history_current_idx == -1) {
                history_current_idx = (history_write_idx + HISTORY_MAX - 1) % HISTORY_MAX;
            } else {
                int next_back = (history_current_idx + HISTORY_MAX - 1) % HISTORY_MAX;
                int oldest_idx = history_count < HISTORY_MAX ? 0 : history_write_idx;
                if (history_current_idx != oldest_idx) {
                    history_current_idx = next_back;
                }
            }
            set_input_line(history[history_current_idx]);
        }
    }
    else if (scancode == 0x50) { 
        if (history_current_idx != -1) {
            int next_forward = (history_current_idx + 1) % HISTORY_MAX;
            if (next_forward == history_write_idx) {
                history_current_idx = -1;
                set_input_line("");
            } else {
                history_current_idx = next_forward;
                set_input_line(history[history_current_idx]);
            }
        }
    }
    else if (scancode == 0x0F) {
        autocomplete_input_line();
    }
    else {
        char c = is_shift ? scancode_map_shift[scancode] : scancode_map[scancode];
        if (capslock_active && c >= 'a' && c <= 'z') c -= 32;
        else if (capslock_active && c >= 'A' && c <= 'Z') c += 32;
        if (c != 0 && input_pos < INPUT_BUF_SIZE - 1) {
            input_buf[input_pos++] = c;
            vga_putchar(c);
        }
    }
    outb(0x20, 0x20);
}
