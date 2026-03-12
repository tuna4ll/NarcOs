#include <stdint.h>
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
extern volatile int snake_running;
extern volatile int snake_next_dir;
extern volatile int editor_running;
extern volatile int editor_input_key;
extern volatile int editor_special_key;
extern int win_visible;
void handle_keyboard()
{
    if (!win_visible && !snake_running && !editor_running) {
        uint8_t scancode = inb(0x60);
        outb(0x20, 0x20);
        return;
    }
    uint8_t scancode = inb(0x60);
    if (scancode & 0x80) {
        uint8_t key = scancode & 0x7F;
        if      (key == 0x2A) lshift_pressed = 0;
        else if (key == 0x36) rshift_pressed = 0;
        outb(0x20, 0x20);
        return;
    }
    if (snake_running) {
        switch (scancode) {
            case 0x11: case 0x48: snake_next_dir = 0; break;
            case 0x1F: case 0x50: snake_next_dir = 1; break;
            case 0x1E: case 0x4B: snake_next_dir = 2; break;
            case 0x20: case 0x4D: snake_next_dir = 3; break;
            case 0x13:            snake_next_dir = 5; break;
            case 0x10: case 0x01: snake_next_dir = 6; break;
            default: break;
        }
        outb(0x20, 0x20);
        return;
    }
    if (editor_running) {
        int is_shift = lshift_pressed || rshift_pressed;
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
    if      (scancode == 0x2A) lshift_pressed  = 1;
    else if (scancode == 0x36) rshift_pressed  = 1;
    else if (scancode == 0x3A) capslock_active = !capslock_active;
    else if (scancode == 0x0E) {
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
            while (input_pos > 0) {
                input_pos--;
                vga_backspace();
            }
            int k = 0;
            while (history[history_current_idx][k] != '\0') {
                char c = history[history_current_idx][k];
                input_buf[input_pos++] = c;
                vga_putchar(c);
                k++;
            }
            input_buf[input_pos] = '\0';
        }
    }
    else if (scancode == 0x50) { 
        if (history_current_idx != -1) {
            int next_forward = (history_current_idx + 1) % HISTORY_MAX;
            while (input_pos > 0) {
                input_pos--;
                vga_backspace();
            }
            if (next_forward == history_write_idx) {
                history_current_idx = -1;
                input_pos = 0;
                input_buf[0] = '\0';
            } else {
                history_current_idx = next_forward;
                int k = 0;
                while (history[history_current_idx][k] != '\0') {
                    char c = history[history_current_idx][k];
                    input_buf[input_pos++] = c;
                    vga_putchar(c);
                    k++;
                }
                input_buf[input_pos] = '\0';
            }
        }
    }
    else {
        int  is_shift = lshift_pressed || rshift_pressed;
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