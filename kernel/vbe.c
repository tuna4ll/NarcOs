#include <stdint.h>
#include "vbe.h"
#include "string.h"
typedef struct {
    uint16_t attributes;
    uint8_t  win_a, win_b;
    uint16_t granularity;
    uint16_t winsize;
    uint16_t segment_a, segment_b;
    uint32_t real_fct_ptr;
    uint16_t pitch;
    uint16_t width;
    uint16_t height;
    uint8_t  w_char, y_char, planes, bpp, banks;
    uint8_t  memory_model, bank_size, image_pages;
    uint8_t  reserved0;
    uint8_t  red_mask, red_position;
    uint8_t  green_mask, green_position;
    uint8_t  blue_mask, blue_position;
    uint8_t  rsv_mask, rsv_position;
    uint8_t  direct_color_attributes;
    uint32_t framebuffer;
    uint32_t off_screen_mem_off;
    uint16_t off_screen_mem_size;
    uint8_t  reserved1[206];
} __attribute__((packed)) vbe_mode_info_t;

vbe_mode_info_t* mode_info = (vbe_mode_info_t*)0x6100;

extern uint8_t inb(uint16_t port);
extern void outb(uint16_t port, uint8_t val);

static uint8_t* backbuffer = (uint8_t*)0x800000;
static uint8_t* wallpaper_buffer = (uint8_t*)0x1000000;
static uint8_t* window_buffer = (uint8_t*)0x1800000;
static uint8_t* composition_buffer = (uint8_t*)0x2000000;
static int wallpaper_init = 0;

volatile int gui_needs_redraw = 1;

static uint8_t* current_target = (uint8_t*)0x800000;
static uint32_t current_target_width = 0;

unsigned char vbe_font[256][8] = {
    [0] = {0},
    ['0'] = {0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0x00},
    ['1'] = {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00},
    ['2'] = {0x3C, 0x66, 0x06, 0x0C, 0x18, 0x30, 0x7E, 0x00},
    ['3'] = {0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00},
    ['4'] = {0x0C, 0x1C, 0x3C, 0x6C, 0x7E, 0x0C, 0x0C, 0x00},
    ['5'] = {0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00},
    ['6'] = {0x3C, 0x66, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0x00},
    ['7'] = {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00},
    ['8'] = {0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00},
    ['9'] = {0x3C, 0x66, 0x66, 0x3E, 0x06, 0x66, 0x3C, 0x00},
    ['A'] = {0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x00},
    ['B'] = {0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00},
    ['C'] = {0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00},
    ['D'] = {0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00},
    ['E'] = {0x7E, 0x60, 0x60, 0x78, 0x60, 0x60, 0x7E, 0x00},
    ['F'] = {0x7E, 0x60, 0x60, 0x78, 0x60, 0x60, 0x60, 0x00},
    ['G'] = {0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3C, 0x00},
    ['H'] = {0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00},
    ['I'] = {0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00},
    ['J'] = {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38, 0x00},
    ['K'] = {0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x00},
    ['L'] = {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00},
    ['M'] = {0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x00},
    ['N'] = {0x66, 0x76, 0x7E, 0x7E, 0x7E, 0x6E, 0x66, 0x00},
    ['O'] = {0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00},
    ['P'] = {0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x00},
    ['Q'] = {0x3C, 0x66, 0x66, 0x66, 0x6E, 0x3C, 0x0E, 0x00},
    ['R'] = {0x7C, 0x66, 0x66, 0x7C, 0x78, 0x6C, 0x66, 0x00},
    ['S'] = {0x3C, 0x66, 0x30, 0x18, 0x0C, 0x66, 0x3C, 0x00},
    ['T'] = {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00},
    ['U'] = {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00},
    ['V'] = {0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00},
    ['W'] = {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00},
    ['X'] = {0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0x00},
    ['Y'] = {0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x00},
    ['Z'] = {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E, 0x00},
    ['a'] = {0x00, 0x00, 0x3C, 0x06, 0x3E, 0x66, 0x3B, 0x00},
    ['b'] = {0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x7C, 0x00},
    ['c'] = {0x00, 0x00, 0x3C, 0x60, 0x60, 0x66, 0x3C, 0x00},
    ['d'] = {0x06, 0x06, 0x3E, 0x66, 0x66, 0x66, 0x3E, 0x00},
    ['e'] = {0x00, 0x00, 0x3C, 0x66, 0x7E, 0x60, 0x3C, 0x00},
    ['f'] = {0x1C, 0x36, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x00},
    ['g'] = {0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x3C},
    ['h'] = {0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00},
    ['i'] = {0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00},
    ['j'] = {0x06, 0x00, 0x06, 0x06, 0x06, 0x06, 0x66, 0x3C},
    ['k'] = {0x60, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0x00},
    ['l'] = {0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00},
    ['m'] = {0x00, 0x00, 0x6B, 0x7F, 0x7F, 0x6B, 0x63, 0x00},
    ['n'] = {0x00, 0x00, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00},
    ['o'] = {0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x3C, 0x00},
    ['p'] = {0x00, 0x00, 0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60},
    ['q'] = {0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x06},
    ['r'] = {0x00, 0x00, 0x7C, 0x66, 0x60, 0x60, 0x60, 0x00},
    ['s'] = {0x00, 0x00, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x00},
    ['t'] = {0x18, 0x18, 0x7E, 0x18, 0x18, 0x18, 0x0E, 0x00},
    ['u'] = {0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3E, 0x00},
    ['v'] = {0x00, 0x00, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00},
    ['w'] = {0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00},
    ['x'] = {0x00, 0x00, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x00},
    ['y'] = {0x00, 0x00, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x3C},
    ['z'] = {0x00, 0x00, 0x7E, 0x0C, 0x18, 0x30, 0x7E, 0x00},
    [' '] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    [':'] = {0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00},
    ['/'] = {0x00, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x00, 0x00},
    ['.'] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00},
};

uint16_t mouse_cursor_bitmap[12] = {
    0b110000000000, 0b111000000000, 0b111100000000, 0b111110000000,
    0b111111000000, 0b111111100000, 0b111111110000, 0b111111111000,
    0b111111111100, 0b111111000000, 0b110111000000, 0b100011000000
};
uint16_t folder_icon_bitmap[16] = {
    0b0111100000000000, 0b1111110000000000, 0b1111111111111110, 0b1111111111111110,
    0b1100000000000010, 0b1100000000000010, 0b1100000000000010, 0b1100000000000010,
    0b1100000000000010, 0b1100000000000010, 0b1100000000000010, 0b1100000000000010,
    0b1111111111111110, 0b1111111111111110, 0b0000000000000000, 0b0000000000000000
};
uint16_t file_icon_bitmap[16] = {
    0b0011111111110000, 0b0011111111111000, 0b0011111111111100, 0b0011111100111110,
    0b0011111100011110, 0b0011111100001110, 0b0011111111111110, 0b0011111111111110,
    0b0011111111111110, 0b0011111111111110, 0b0011111111111110, 0b0011111111111110,
    0b0011111111111110, 0b0011111111111110, 0b0000000000000000, 0b0000000000000000
};
uint16_t pc_icon_bitmap[16] = {
    0b1111111111111111, 0b1111111111111111, 0b1100000000000011, 0b1100000000000011,
    0b1100001111000011, 0b1100001111000011, 0b1100000000000011, 0b1100000000000011,
    0b1111111111111111, 0b1111111111111111, 0b0000001111000000, 0b0000001111000000,
    0b0001111111111000, 0b0001111111111000, 0b0000000000000000, 0b0000000000000000
};

void* vbe_get_backbuffer() { return backbuffer; }

void vbe_update() {
    if (!mode_info->framebuffer) return;
    uint32_t bpp_bytes = mode_info->bpp / 8;
    uint32_t row_size = mode_info->width * bpp_bytes;
    if (mode_info->pitch == row_size) {
        vbe_memcpy_sse((void*)mode_info->framebuffer, backbuffer, mode_info->width * mode_info->height * bpp_bytes);
    } else {
        for(uint32_t y = 0; y < mode_info->height; y++) {
            vbe_memcpy_sse((void*)(mode_info->framebuffer + y * mode_info->pitch), backbuffer + y * row_size, row_size);
        }
    }
}

void wait_vsync() {
    while (inb(0x3DA) & 8);
    while (!(inb(0x3DA) & 8));
}

void init_vbe() {
    current_target_width = mode_info->width;
    current_target = backbuffer;
    vbe_draw_wallpaper();
    vbe_update();
}

void vbe_set_target(uint8_t* buffer, uint32_t width) {
    current_target = buffer;
    current_target_width = width;
}

void vbe_put_pixel_to(uint8_t* buffer, uint32_t buf_width, int x, int y, uint32_t color) {
    if (x < 0 || (uint32_t)x >= buf_width || y < 0 || (uint32_t)y >= mode_info->height) return;
    uint32_t bpp_bytes = mode_info->bpp / 8;
    int offset = (y * buf_width + x) * bpp_bytes;
    if (mode_info->bpp == 32) {
        *(uint32_t*)(buffer + offset) = color;
    } else if (mode_info->bpp == 24) {
        buffer[offset]     = (color) & 0xFF;
        buffer[offset + 1] = (color >> 8) & 0xFF;
        buffer[offset + 2] = (color >> 16) & 0xFF;
    } else if (mode_info->bpp == 16) {
        uint16_t r = (color >> 16) & 0xFF;
        uint16_t g = (color >> 8) & 0xFF;
        uint16_t b = color & 0xFF;
        *(uint16_t*)(buffer + offset) = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }
}

void vbe_put_pixel(int x, int y, uint32_t color) {
    vbe_put_pixel_to(current_target, current_target_width, x, y, color);
}

void vbe_draw_wallpaper() {
    uint32_t bpp_bytes = mode_info->bpp / 8;
    uint32_t screen_size = mode_info->width * mode_info->height * bpp_bytes;
    if (!wallpaper_init) {
        for (uint32_t y = 0; y < mode_info->height; y++) {
            uint8_t r = (y * 50) / mode_info->height;
            uint8_t g = (y * 20) / mode_info->height;
            uint8_t b = 100 + (y * 100) / mode_info->height;
            uint32_t color = (r << 16) | (g << 8) | b;
            for (uint32_t x = 0; x < mode_info->width; x++) {
                vbe_put_pixel_to(wallpaper_buffer, mode_info->width, x, y, color);
            }
        }
        wallpaper_init = 1;
    }
    vbe_memcpy_sse(backbuffer, wallpaper_buffer, screen_size);
}

void vbe_draw_cursor(int x, int y) {
    for (int i = 0; i < 12; i++) {
        for (int j = 0; j < 12; j++) {
            if (mouse_cursor_bitmap[i] & (1 << (11 - j))) vbe_put_pixel(x + j, y + i, 0xFFFFFF);
        }
    }
}

void vbe_clear(uint32_t color) {
    vbe_fill_rect(0, 0, mode_info->width, mode_info->height, color);
}

void vbe_draw_char(int x, int y, char c, uint32_t color) {
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (vbe_font[(uint8_t)c][row] & (1 << (7 - col))) vbe_put_pixel(x + col, y + row, color);
        }
    }
}

void vbe_draw_string(int x, int y, const char* s, uint32_t color) {
    int cur_x = x;
    while (*s) {
        vbe_draw_char(cur_x, y, *s++, color);
        cur_x += 9;
    }
}

void vbe_render_mouse(int x, int y) { vbe_draw_cursor(x, y); }

void vbe_render_mouse_direct(int x, int y) {
    uint8_t* old_target = current_target;
    uint32_t old_width = current_target_width;
    current_target = (uint8_t*)mode_info->framebuffer;
    current_target_width = mode_info->width;
    vbe_draw_cursor(x, y);
    current_target = old_target;
    current_target_width = old_width;
}
void vbe_copy_to_buffer(void* source) {
    uint32_t size = mode_info->width * mode_info->height * (mode_info->bpp / 8);
    vbe_memcpy(backbuffer, source, size);
}

void vbe_blit_window(int x, int y, int w, int h, uint8_t* win_buf) {
    uint32_t bpp = mode_info->bpp / 8;
    for (int i = 0; i < h; i++) {
        if (y + i < 0 || (uint32_t)(y + i) >= mode_info->height) continue;
        uint32_t draw_x = (x < 0) ? 0 : x;
        uint32_t win_off_x = (x < 0) ? -x : 0;
        uint32_t draw_w = (x + w > (int)mode_info->width) ? (mode_info->width - draw_x) : (w - win_off_x);
        if (draw_w > 0) {
            uint8_t* dest = backbuffer + ((y + i) * mode_info->width + draw_x) * bpp;
            uint8_t* src  = win_buf + (i * w + win_off_x) * bpp;
            vbe_memcpy_sse(dest, src, draw_w * bpp);
        }
    }
}

void vbe_blit_rect(int x, int y, int w, int h, uint8_t* src_buf, uint32_t src_stride) {
    uint32_t bpp = mode_info->bpp / 8;
    for (int i = 0; i < h; i++) {
        if (y + i < 0 || (uint32_t)(y + i) >= mode_info->height) continue;
        if (x < 0 || (uint32_t)x >= mode_info->width) continue;
        uint32_t draw_w = (x + w > (int)mode_info->width) ? (mode_info->width - x) : w;
        uint8_t* dest = (uint8_t*)mode_info->framebuffer + ((y + i) * mode_info->pitch + x * bpp);
        uint8_t* src  = src_buf + ((y + i) * src_stride + x) * bpp;
        vbe_memcpy_sse(dest, src, draw_w * bpp);
    }
}

void vbe_fill_rect(int x, int y, int w, int h, uint32_t color) {
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) vbe_put_pixel(x + j, y + i, color);
    }
}
void vbe_draw_taskbar(int start_btn_active) {
    uint32_t w = mode_info->width;
    uint32_t tb_h = 35;
    for (uint32_t i = 0; i < tb_h; i++) {
        uint32_t color = (20 << 16) | (20 << 8) | 30; 
        vbe_fill_rect(0, i, w, 1, color);
    }
    vbe_fill_rect(0, tb_h - 1, w, 1, 0x444444);
    uint32_t btn_color = start_btn_active ? 0x00AAFF : 0x222222;
    vbe_fill_rect(5, 3, 70, 28, btn_color);
    vbe_draw_string(15, 12, "NARC", 0xFFFFFF);
    vbe_fill_rect(80, 3, 90, 28, 0x333333);
    vbe_draw_string(90, 12, "TERMINAL", 0xAAAAAA);
}
void vbe_draw_start_menu() {
    vbe_fill_rect(5, 35, 200, 300, 0x111111);
    vbe_draw_rect(5, 35, 200, 300, 0x444444);
    vbe_draw_string(20, 50, "APPLICATIONS", 0x00AAFF);
    vbe_draw_string(20, 80, "> Terminal", 0xFFFFFF);
    vbe_draw_string(20, 100, "> Snake", 0xFFFFFF);
    vbe_draw_string(20, 120, "> NarcVim", 0xFFFFFF);
    vbe_fill_rect(5, 280, 200, 1, 0x333333);
    vbe_draw_string(20, 300, "SYSTEM INFO", 0x00AAFF);
    vbe_draw_string(20, 320, "NarcOs v1.0", 0xAAAAAA);
}
extern uint8_t get_hour();
extern uint8_t get_minute();
extern uint8_t get_second();
void vbe_draw_clock() {
    uint32_t w = mode_info->width;
    char time_str[9];
    uint8_t hh = get_hour();
    uint8_t mm = get_minute();
    uint8_t ss = get_second();
    time_str[0] = (hh / 10) + '0'; time_str[1] = (hh % 10) + '0'; time_str[2] = ':';
    time_str[3] = (mm / 10) + '0'; time_str[4] = (mm % 10) + '0'; time_str[5] = ':';
    time_str[6] = (ss / 10) + '0'; time_str[7] = (ss % 10) + '0'; time_str[8] = '\0';
    vbe_draw_string(w - 90, 12, time_str, 0x00FF00);
}
void vbe_draw_icon(int x, int y, int type, const char* label, int selected) {
    uint16_t* bitmap;
    uint32_t color;
    if (type == 0) { bitmap = folder_icon_bitmap; color = 0xFFAA00; }
    else if (type == 1) { bitmap = file_icon_bitmap; color = 0xAAAAAA; }
    else { bitmap = pc_icon_bitmap; color = 0x00AAFF; }
    if (selected) vbe_fill_rect(x - 5, y - 5, 40, 50, (60 << 16) | (60 << 8) | 80);
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 16; j++) {
            if (bitmap[i] & (1 << (15 - j))) {
                vbe_put_pixel(x + j * 2, y + i * 2, color);
                vbe_put_pixel(x + j * 2 + 1, y + i * 2, color);
                vbe_put_pixel(x + j * 2, y + i * 2 + 1, color);
                vbe_put_pixel(x + j * 2 + 1, y + i * 2 + 1, color);
            }
        }
    }
    int label_x = x + 16 - (strlen(label) * 9) / 2;
    vbe_draw_string(label_x, y + 35, label, 0xFFFFFF);
}
#include "fs.h"
extern disk_fs_node_t dir_cache[MAX_FILES];
void vbe_draw_explorer_content(int wx, int wy, int current_dir) {
    int x_off = 20, y_off = 40;
    int col = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (dir_cache[i].flags != 0 && dir_cache[i].parent_index == current_dir) {
            int type = (dir_cache[i].flags == 2) ? 0 : 1;
            vbe_draw_icon(wx + x_off + col * 80, wy + y_off, type, dir_cache[i].name, 0);
            col++;
            if (col > 7) { col = 0; y_off += 70; }
        }
    }
}
void vbe_draw_desktop_icons() {
    vbe_draw_icon(20, 60, 2, "This PC", 0);
    vbe_draw_icon(20, 140, 1, "Terminal", 0);
    vbe_draw_icon(20, 220, 0, "Users", 0);
}

void vbe_draw_rect(int x, int y, int w, int h, uint32_t color) {
    for (int j = 0; j < w; j++) {
        vbe_put_pixel(x + j, y, color);
        vbe_put_pixel(x + j, y + h - 1, color);
    }
    for (int i = 0; i < h; i++) {
        vbe_put_pixel(x, y + i, color);
        vbe_put_pixel(x + w - 1, y + i, color);
    }
}

uint32_t vbe_get_width()  { return mode_info->width; }
uint32_t vbe_get_height() { return mode_info->height; }
uint32_t vbe_get_bpp()    { return mode_info->bpp; }

void* vbe_get_window_buffer() { return window_buffer; }

void vbe_draw_breadcrumb(int x, int y, int current_dir) {
    vbe_fill_rect(x, y, 600, 20, 0x222222);
    vbe_draw_string(x + 5, y + 5, "Path: /", 0xAAAAAA);
    if (current_dir != -1) {
        vbe_draw_string(x + 55, y + 5, dir_cache[current_dir].name, 0xFFFFFF);
    }
}
void vbe_draw_narcpad(int x, int y, const char* title, const char* content) {
    vbe_fill_rect(x, y, 500, 400, 0xFCFCFC);
    vbe_draw_rect(x, y, 500, 400, 0x888888);
    vbe_fill_rect(x, y, 500, 25, 0xDDDDDD);
    vbe_draw_string(x + 10, y + 5, title, 0x333333);
    vbe_fill_rect(x + 500 - 20, y + 5, 12, 12, 0xFF5555);
    int line_y = y + 40;
    char line_buf[55];
    int char_idx = 0;
    while (*content && line_y < y + 380) {
        if (*content == '\n' || char_idx == 54) {
            line_buf[char_idx] = '\0';
            vbe_draw_string(x + 15, line_y, line_buf, 0x000000);
            line_y += 15;
            char_idx = 0;
            if (*content == '\n') { content++; continue; }
        }
        line_buf[char_idx++] = *content++;
    }
    line_buf[char_idx] = '\0';
    if (char_idx > 0) vbe_draw_string(x + 15, line_y, line_buf, 0x000000);
}
void vbe_compose_scene(int wx, int wy, int win_vis, int start_vis, int exp_vis, int exp_x, int exp_y, int exp_dir, int pad_vis, int pad_x, int pad_y, const char* pad_title, const char* pad_content) {
    uint32_t bpp_bytes = mode_info->bpp / 8;
    uint32_t size = mode_info->width * mode_info->height * bpp_bytes;
    vbe_memcpy_sse(composition_buffer, wallpaper_buffer, size);
    uint8_t* old_target = current_target;
    uint32_t old_width = current_target_width;
    vbe_set_target(composition_buffer, mode_info->width);
    vbe_draw_desktop_icons();
    uint8_t* old_back = backbuffer;
    backbuffer = composition_buffer;
    if (win_vis) vbe_blit_window(wx, wy, 700, 475, window_buffer);
    if (exp_vis) {
        vbe_fill_rect(exp_x, exp_y, 600, 400, 0x111111);
        vbe_draw_rect(exp_x, exp_y, 600, 400, 0x444444);
        vbe_fill_rect(exp_x, exp_y, 600, 25, 0x333333);
        vbe_draw_string(exp_x + 10, exp_y + 5, "NarcExplorer", 0xFFFFFF);
        vbe_fill_rect(exp_x + 600 - 20, exp_y + 5, 12, 12, 0xFF5555);
        vbe_draw_breadcrumb(exp_x, exp_y + 25, exp_dir);
        vbe_draw_explorer_content(exp_x, exp_y + 15, exp_dir);
    }
    if (pad_vis) vbe_draw_narcpad(pad_x, pad_y, pad_title, pad_content);
    vbe_draw_taskbar(start_vis);
    if (start_vis) vbe_draw_start_menu();
    vbe_draw_clock();
    backbuffer = old_back;
    vbe_set_target(old_target, old_width);
    gui_needs_redraw = 0;
}

void vbe_prepare_frame_from_composition() {
    uint32_t bpp_bytes = mode_info->bpp / 8;
    uint32_t size = mode_info->width * mode_info->height * bpp_bytes;
    vbe_memcpy_sse(backbuffer, composition_buffer, size);
}
