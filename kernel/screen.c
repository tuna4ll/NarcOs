#include <stdint.h>
#include "vbe.h"
#define WIN_WIDTH  700
#define WIN_HEIGHT 450
#define WIN_TITLE_H 25
#define WIN_TITLE_H 25
static int win_x = 150;
static int win_y = 120;
int win_visible = 0;
void vga_set_window_pos(int x, int y) {
    win_x = x;
    win_y = y;
}
int vga_get_window_x() { return win_x; }
int vga_get_window_y() { return win_y; }
int vga_get_window_w() { return WIN_WIDTH; }
int vga_get_window_h() { return WIN_HEIGHT + WIN_TITLE_H; }
int vga_get_title_h()  { return WIN_TITLE_H; }
void* vga_get_window_buffer() { return vbe_get_window_buffer(); }
#define WIN_COLS (WIN_WIDTH / 9)
#define WIN_ROWS (WIN_HEIGHT / 12)
static int cursor_x = 0;
static int cursor_y = 0;
typedef struct {
    char c;
    uint8_t color;
} screen_char_t;
static screen_char_t text_buffer[WIN_ROWS][WIN_COLS];
void vga_prepare_win_draw() {
    vbe_set_target(vbe_get_window_buffer(), WIN_WIDTH);
}
void vga_redraw_text_to_buffer() {
    for (int y = 0; y < WIN_ROWS; y++) {
        for (int x = 0; x < WIN_COLS; x++) {
            if (text_buffer[y][x].c != 0) {
                uint32_t rgb = 0xAAAAAA;
                uint8_t col = text_buffer[y][x].color & 0x0F;
                switch(col) {
                    case 0x00: rgb = 0x000000; break;
                    case 0x0A: rgb = 0x55FF55; break;
                    case 0x0B: rgb = 0x55FFFF; break;
                    case 0x0C: rgb = 0xFF5555; break;
                    case 0x0E: rgb = 0xFFFF55; break;
                    case 0x0F: rgb = 0xFFFFFF; break;
                    default: rgb = 0xAAAAAA; break;
                }
                vbe_draw_char(x * 9 + 5, WIN_TITLE_H + y * 12 + 5, text_buffer[y][x].c, rgb);
            }
        }
    }
}
void draw_window_frame_to_buffer() {
    vga_prepare_win_draw();
    vbe_fill_rect(0, WIN_TITLE_H, WIN_WIDTH, WIN_HEIGHT, 0x1A1A1A);
    vbe_fill_rect(0, 0, WIN_WIDTH, WIN_TITLE_H, 0x333333);
    vbe_draw_rect(0, 0, WIN_WIDTH, WIN_HEIGHT + WIN_TITLE_H, 0x555555);
    vbe_draw_string(10, 5, "NarcOs Terminal", 0xFFFFFF);
    vbe_fill_rect(WIN_WIDTH - 20, 5, 12, 12, 0xFF5555);
}
void vga_refresh_window() {
    draw_window_frame_to_buffer();
    vga_redraw_text_to_buffer();
    vbe_set_target(vbe_get_backbuffer(), vbe_get_width()); 
    gui_needs_redraw = 1;
}
void vga_scroll() {
    for (int y = 0; y < WIN_ROWS - 1; y++) {
        for (int x = 0; x < WIN_COLS; x++) {
            text_buffer[y][x] = text_buffer[y+1][x];
        }
    }
    for (int x = 0; x < WIN_COLS; x++) text_buffer[WIN_ROWS-1][x].c = 0;
    cursor_y = WIN_ROWS - 2;
    vga_refresh_window();
}
void clear_screen() {
    for (int y = 0; y < WIN_ROWS; y++) {
        for (int x = 0; x < WIN_COLS; x++) text_buffer[y][x].c = 0;
    }
    cursor_x = 0;
    cursor_y = 0;
    vga_refresh_window();
}
void vga_newline() {
    cursor_x = 0;
    cursor_y++;
    if (cursor_y >= WIN_ROWS - 1) vga_scroll();
}
void vga_putchar_color(char c, uint8_t color) {
    if (c == '\n' || c == '\r') { vga_newline(); return; }
    uint32_t rgb = 0xFFFFFF; 
    switch(color & 0x0F) {
        case 0x00: rgb = 0x000000; break; case 0x01: rgb = 0x0000AA; break; case 0x02: rgb = 0x00AA00; break; 
        case 0x03: rgb = 0x00AAAA; break; case 0x04: rgb = 0xAA0000; break; case 0x05: rgb = 0xAA00AA; break; 
        case 0x06: rgb = 0xAA5500; break; case 0x07: rgb = 0xAAAAAA; break; case 0x08: rgb = 0x555555; break; 
        case 0x09: rgb = 0x5555FF; break; case 0x0A: rgb = 0x55FF55; break; case 0x0B: rgb = 0x55FFFF; break; 
        case 0x0C: rgb = 0xFF5555; break; case 0x0D: rgb = 0xFF55FF; break; case 0x0E: rgb = 0xFFFF55; break; 
        case 0x0F: rgb = 0xFFFFFF; break; 
    }
    text_buffer[cursor_y][cursor_x].c = c;
    text_buffer[cursor_y][cursor_x].color = color;
    vga_prepare_win_draw();
    vbe_draw_char(cursor_x * 9 + 5, WIN_TITLE_H + cursor_y * 12 + 5, c, rgb);
    vbe_set_target(vbe_get_backbuffer(), vbe_get_width());
    gui_needs_redraw = 1;
    cursor_x++;
    if (cursor_x >= WIN_COLS - 1) vga_newline();
}
void vga_putchar(char c) { vga_putchar_color(c, 0x0A); }
void vga_print(const char* str) { while (*str) vga_putchar(*str++); }
void vga_println(const char* str) { vga_print(str); vga_newline(); }
void vga_print_color(const char* str, uint8_t color) { while (*str) vga_putchar_color(*str++, color); }
void vga_backspace() {
    if (cursor_x > 0) {
        cursor_x--;
        text_buffer[cursor_y][cursor_x].c = 0;
        vga_prepare_win_draw();
        vbe_fill_rect(cursor_x * 9 + 5, WIN_TITLE_H + cursor_y * 12 + 5, 9, 12, 0x1A1A1A);
        vbe_set_target(vbe_get_backbuffer(), vbe_get_width());
        gui_needs_redraw = 1;
    }
}
void vga_print_int(int num) {
    if (num == 0) { vga_putchar('0'); return; }
    if (num < 0) { vga_putchar('-'); num = -num; }
    char buf[16]; int i = 0;
    while (num > 0) { buf[i++] = (num % 10) + '0'; num /= 10; }
    for (int j = i - 1; j >= 0; j--) vga_putchar(buf[j]);
}
