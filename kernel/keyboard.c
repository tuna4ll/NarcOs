// kernel/keyboard.c
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


void handle_keyboard()
{
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

        for (int i = 0; i < INPUT_BUF_SIZE; i++)
            cmd_to_execute[i] = input_buf[i];

        cmd_ready = 1;
        input_pos = 0;
        for (int i = 0; i < INPUT_BUF_SIZE; i++) input_buf[i] = 0;
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