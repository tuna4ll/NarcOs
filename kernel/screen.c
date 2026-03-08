// kernel/screen.c

#include <stdint.h>

#define VGA_BASE 0xB8000
#define VGA_COLS 80
#define VGA_ROWS 25

#define COLOR_NORMAL 0x0A
#define COLOR_WARN   0x0E
#define COLOR_ERR    0x0C
#define COLOR_BRIGHT 0x0B
#define COLOR_DIM    0x08

extern void outb(uint16_t port, uint8_t val);

void update_hw_cursor();
void clear_screen();
void vga_newline();
void vga_putchar_color(char c, uint8_t color);
void vga_putchar(char c);
void vga_print(const char* str);
void vga_println(const char* str);
void vga_print_color(const char* str, uint8_t color);
void vga_backspace();
void vga_print_int(int num);

int cursor_x = 0;
int cursor_y = 0;

void update_hw_cursor() {
    uint16_t pos = cursor_y * VGA_COLS + cursor_x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void vga_scroll() {
    volatile uint16_t* vga = (volatile uint16_t*)VGA_BASE;
    for (int y = 0; y < VGA_ROWS - 1; y++) {
        for (int x = 0; x < VGA_COLS; x++) {
            vga[y * VGA_COLS + x] = vga[(y + 1) * VGA_COLS + x];
        }
    }
    for (int x = 0; x < VGA_COLS; x++) {
        vga[(VGA_ROWS - 1) * VGA_COLS + x] = (uint16_t)(' ' | (COLOR_NORMAL << 8));
    }
    cursor_y = VGA_ROWS - 1;
}

void clear_screen() {
    volatile uint16_t* vga = (volatile uint16_t*)VGA_BASE;
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++) {
        vga[i] = (uint16_t)(' ' | (COLOR_NORMAL << 8));
    }
    cursor_x = 0;
    cursor_y = 0;
    update_hw_cursor();
}

void vga_newline() {
    cursor_x = 0;
    cursor_y++;
    if (cursor_y >= VGA_ROWS) {
        vga_scroll();
    }
    update_hw_cursor();
}

void vga_putchar_color(char c, uint8_t color) {
    if (c == '\n' || c == '\r') {
        vga_newline();
        return;
    }

    volatile uint16_t* vga = (volatile uint16_t*)VGA_BASE;
    int index = cursor_y * VGA_COLS + cursor_x;
    
    vga[index] = (uint16_t)(c | (color << 8));
    
    cursor_x++;
    if (cursor_x >= VGA_COLS) {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= VGA_ROWS) {
            vga_scroll();
        }
    }
    update_hw_cursor();
}

void vga_putchar(char c) {
    vga_putchar_color(c, COLOR_NORMAL);
}

void vga_print(const char* str) {
    int i = 0;
    while (str[i] != '\0') {
        vga_putchar(str[i]);
        i++;
    }
}

void vga_println(const char* str) {
    vga_print(str);
    vga_newline();
}

void vga_print_color(const char* str, uint8_t color) {
    int i = 0;
    while (str[i] != '\0') {
        vga_putchar_color(str[i], color);
        i++;
    }
}

void vga_backspace() {
    if (cursor_x > 0) {
        cursor_x--;
    } else if (cursor_y > 0) {
        cursor_y--;
        cursor_x = VGA_COLS - 1;
    } else {
        return;
    }

    volatile uint16_t* vga = (volatile uint16_t*)VGA_BASE;
    int index = cursor_y * VGA_COLS + cursor_x;
    vga[index] = (uint16_t)(' ' | (COLOR_NORMAL << 8));
    update_hw_cursor();
}

void vga_print_int(int num) {
    if (num == 0) {
        vga_putchar('0');
        return;
    }
    if (num < 0) {
        vga_putchar('-');
        num = -num;
    }
    
    char buf[16];
    int pos = 0;
    while (num > 0) {
        buf[pos++] = (char)((num % 10) + '0');
        num /= 10;
    }
    
    for (int i = pos - 1; i >= 0; i--) {
        vga_putchar(buf[i]);
    }
}
