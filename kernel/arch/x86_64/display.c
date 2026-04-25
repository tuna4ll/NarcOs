#include <stddef.h>
#include <stdint.h>

#include "paging.h"
#include "serial.h"
#include "string.h"
#include "vbe.h"
#include "x64_paging.h"

typedef struct {
    uint16_t attributes;
    uint8_t win_a;
    uint8_t win_b;
    uint16_t granularity;
    uint16_t winsize;
    uint16_t segment_a;
    uint16_t segment_b;
    uint32_t real_fct_ptr;
    uint16_t pitch;
    uint16_t width;
    uint16_t height;
    uint8_t w_char;
    uint8_t y_char;
    uint8_t planes;
    uint8_t bpp;
    uint8_t banks;
    uint8_t memory_model;
    uint8_t bank_size;
    uint8_t image_pages;
    uint8_t reserved0;
    uint8_t red_mask;
    uint8_t red_position;
    uint8_t green_mask;
    uint8_t green_position;
    uint8_t blue_mask;
    uint8_t blue_position;
    uint8_t rsv_mask;
    uint8_t rsv_position;
    uint8_t direct_color_attributes;
    uint32_t framebuffer;
    uint32_t off_screen_mem_off;
    uint16_t off_screen_mem_size;
    uint8_t reserved1[206];
} __attribute__((packed)) x64_vbe_mode_info_t;

static x64_vbe_mode_info_t* const mode_info = (x64_vbe_mode_info_t*)(uintptr_t)0x6100U;
static uint8_t* framebuffer = 0;
static uint32_t fb_width = 0;
static uint32_t fb_height = 0;
static uint32_t fb_bpp = 0;
static uint32_t fb_pitch = 0;
static uint32_t fb_bytes_per_pixel = 0;
static int graphics_enabled = 0;

volatile int gui_needs_redraw = 0;
window_t windows[MAX_WINDOWS];
int window_count = 0;
int active_window_idx = -1;
volatile int snk_next_dir = -1;
int editor_running = 0;
int editor_input_key = 0;
int editor_special_key = 0;

static void display_write_u32_decimal(uint32_t value) {
    char buf[16];
    int index = 15;

    buf[index] = '\0';
    if (value == 0U) {
        serial_write("0");
        return;
    }
    while (value != 0U && index > 0) {
        buf[--index] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    serial_write(&buf[index]);
}

static void display_put_pixel_raw(int x, int y, uint32_t color) {
    uint8_t* pixel;

    if (!graphics_enabled || !framebuffer) return;
    if (x < 0 || y < 0 || (uint32_t)x >= fb_width || (uint32_t)y >= fb_height) return;

    pixel = framebuffer + (size_t)y * fb_pitch + (size_t)x * fb_bytes_per_pixel;
    if (fb_bytes_per_pixel >= 4U) {
        *(uint32_t*)pixel = color;
    } else if (fb_bytes_per_pixel == 3U) {
        pixel[0] = (uint8_t)(color & 0xFFU);
        pixel[1] = (uint8_t)((color >> 8) & 0xFFU);
        pixel[2] = (uint8_t)((color >> 16) & 0xFFU);
    } else if (fb_bytes_per_pixel == 2U) {
        *(uint16_t*)pixel = (uint16_t)color;
    } else if (fb_bytes_per_pixel == 1U) {
        *pixel = (uint8_t)color;
    }
}

void init_vbe(void) {
    uint64_t framebuffer_phys;
    size_t framebuffer_size;

    fb_width = mode_info->width;
    fb_height = mode_info->height;
    fb_bpp = mode_info->bpp;
    fb_pitch = mode_info->pitch;
    fb_bytes_per_pixel = (fb_bpp + 7U) / 8U;
    framebuffer_phys = (uint64_t)mode_info->framebuffer;

    if (fb_width == 0U || fb_height == 0U || fb_bpp == 0U || fb_pitch == 0U ||
        fb_bytes_per_pixel == 0U || framebuffer_phys == 0U) {
        graphics_enabled = 0;
        framebuffer = 0;
        return;
    }

    framebuffer_size = (size_t)fb_pitch * (size_t)fb_height;
    framebuffer = (uint8_t*)x64_paging_map_physical(framebuffer_phys, framebuffer_size,
                                                    X64_PAGING_FLAG_WRITE | X64_PAGING_FLAG_CACHE_DISABLE);
    if (!framebuffer) {
        graphics_enabled = 0;
        return;
    }

    graphics_enabled = 1;
    serial_write("[vbe64] framebuffer=");
    serial_write_hex64(framebuffer_phys);
    serial_write(" width=");
    display_write_u32_decimal(fb_width);
    serial_write(" height=");
    display_write_u32_decimal(fb_height);
    serial_write(" bpp=");
    display_write_u32_decimal(fb_bpp);
    serial_write_char('\n');
}

void vbe_update(void) {}

void vbe_put_pixel(int x, int y, uint32_t color) {
    display_put_pixel_raw(x, y, color);
}

uint32_t vbe_get_pixel(int x, int y) {
    uint8_t* pixel;

    if (!graphics_enabled || !framebuffer) return 0;
    if (x < 0 || y < 0 || (uint32_t)x >= fb_width || (uint32_t)y >= fb_height) return 0;
    pixel = framebuffer + (size_t)y * fb_pitch + (size_t)x * fb_bytes_per_pixel;
    if (fb_bytes_per_pixel >= 4U) return *(uint32_t*)pixel;
    if (fb_bytes_per_pixel == 3U) return (uint32_t)pixel[0] | ((uint32_t)pixel[1] << 8) | ((uint32_t)pixel[2] << 16);
    if (fb_bytes_per_pixel == 2U) return *(uint16_t*)pixel;
    return *pixel;
}

void vbe_clear(uint32_t color) {
    if (!graphics_enabled || !framebuffer) return;
    for (uint32_t y = 0; y < fb_height; y++) {
        for (uint32_t x = 0; x < fb_width; x++) {
            display_put_pixel_raw((int)x, (int)y, color);
        }
    }
}

void vbe_fill_rect(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            display_put_pixel_raw(x + px, y + py, color);
        }
    }
}

void vbe_fill_rect_alpha(int x, int y, int w, int h, uint32_t color, int alpha) {
    (void)alpha;
    vbe_fill_rect(x, y, w, h, color);
}

void vbe_fill_rect_gradient(int x, int y, int w, int h, uint32_t c1, uint32_t c2, int vertical) {
    (void)c2;
    (void)vertical;
    vbe_fill_rect(x, y, w, h, c1);
}

void vbe_draw_rect(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    vbe_fill_rect(x, y, w, 1, color);
    vbe_fill_rect(x, y + h - 1, w, 1, color);
    vbe_fill_rect(x, y, 1, h, color);
    vbe_fill_rect(x + w - 1, y, 1, h, color);
}

void vbe_draw_char_hd(int x, int y, char c, uint32_t color, int scale) {
    (void)c;
    if (scale <= 0) scale = 1;
    vbe_fill_rect(x, y, 6 * scale, 8 * scale, color);
}

void vbe_draw_char(int x, int y, char c, uint32_t color) {
    vbe_draw_char_hd(x, y, c, color, 1);
}

void vbe_draw_string_hd(int x, int y, const char* s, uint32_t color, int scale) {
    int cursor_x = x;

    if (!s) return;
    while (*s != '\0') {
        if (*s != ' ') vbe_draw_char_hd(cursor_x, y, *s, color, scale);
        cursor_x += 8 * scale;
        s++;
    }
}

void vbe_draw_string(int x, int y, const char* s, uint32_t color) {
    vbe_draw_string_hd(x, y, s, color, 1);
}

void vbe_draw_wallpaper(void) {}
void vbe_draw_cursor(int x, int y) { vbe_fill_rect(x, y, 8, 12, 0xFFFFFF); }
void vbe_render_mouse(int x, int y) { vbe_draw_cursor(x, y); }
void vbe_render_mouse_direct(int x, int y) { vbe_draw_cursor(x, y); }
void* vbe_get_backbuffer(void) { return framebuffer; }
void* vbe_get_window_buffer(void) { return framebuffer; }
void vbe_set_target(uint8_t* buffer, uint32_t width) { (void)buffer; (void)width; }
void vbe_compose_scene(window_t* windows_in, int win_count_in, int active_win_idx, int start_vis, int desktop_dir,
                       int drag_file_idx, int mx, int my, int ctx_vis, int ctx_x, int ctx_y,
                       const char** ctx_items, int ctx_count, int ctx_sel) {
    (void)windows_in; (void)win_count_in; (void)active_win_idx; (void)start_vis; (void)desktop_dir;
    (void)drag_file_idx; (void)mx; (void)my; (void)ctx_vis; (void)ctx_x; (void)ctx_y;
    (void)ctx_items; (void)ctx_count; (void)ctx_sel;
}
void vbe_draw_desktop_icons(int desktop_dir) { (void)desktop_dir; }
void vbe_draw_context_menu(int x, int y, const char** items, int count, int selected_idx) {
    (void)x; (void)y; (void)items; (void)count; (void)selected_idx;
}
void vbe_draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color, int alpha) {
    (void)radius; (void)alpha; vbe_fill_rect(x, y, w, h, color);
}
void vbe_draw_shadow(int x, int y, int w, int h, int radius) { (void)x; (void)y; (void)w; (void)h; (void)radius; }
uint32_t vbe_mix_color(uint32_t c1, uint32_t c2, int alpha) { (void)c2; (void)alpha; return c1; }
void vbe_prepare_frame_from_composition(void) {}
void vbe_present_cursor_fast(int old_x, int old_y, int new_x, int new_y) {
    (void)old_x; (void)old_y; vbe_draw_cursor(new_x, new_y);
}
void wait_vsync(void) {}
void vbe_memcpy(void* dest, void* src, uint32_t count) { memcpy(dest, src, count); }
void vbe_blit_window(window_t* win, uint8_t* win_buf, int is_focused) { (void)win; (void)win_buf; (void)is_focused; }
void vbe_get_window_client_rect(window_t* win, int* out_x, int* out_y, int* out_w, int* out_h) {
    if (out_x) *out_x = win ? win->x : 0;
    if (out_y) *out_y = win ? win->y : 0;
    if (out_w) *out_w = win ? win->w : 0;
    if (out_h) *out_h = win ? win->h : 0;
}
void vbe_draw_taskbar(int start_btn_active) { (void)start_btn_active; }
void vbe_draw_start_menu(void) {}
void vbe_draw_clock(void) {}
void vbe_draw_icon(int x, int y, int type, const char* label, int selected) {
    (void)type; (void)label; (void)selected; vbe_fill_rect(x, y, 32, 32, 0xFFFFFF);
}
void vbe_draw_vector_folder(int x, int y, int selected) { (void)selected; vbe_fill_rect(x, y, 24, 18, 0xE0B04A); }
void vbe_draw_vector_file(int x, int y, int selected) { (void)selected; vbe_fill_rect(x, y, 18, 22, 0xD0D8E8); }
void vbe_draw_vector_pc(int x, int y) { vbe_fill_rect(x, y, 28, 18, 0x7CC7FF); }
void vbe_draw_vector_snake(int x, int y) { vbe_fill_rect(x, y, 20, 20, 0x4CAF50); }
void vbe_draw_vector_terminal(int x, int y) { vbe_fill_rect(x, y, 24, 20, 0x2F3B52); }
void vbe_draw_explorer_content(int x, int y, int w, int h, int current_dir) { (void)x; (void)y; (void)w; (void)h; (void)current_dir; }
void vbe_draw_breadcrumb(int x, int y, int w, int current_dir) { (void)x; (void)y; (void)w; (void)current_dir; }
void vbe_draw_narcpad(int x, int y, int w, int h, const char* title, const char* content) {
    (void)title; (void)content; vbe_fill_rect(x, y, w, h, 0xF0F0F0);
}
void vbe_draw_snake_game(int x, int y, int w, int h, int* px, int* py, int len, int ax, int ay, int dead, int score, int best) {
    (void)px; (void)py; (void)len; (void)ax; (void)ay; (void)dead; (void)score; (void)best;
    vbe_fill_rect(x, y, w, h, 0x102010);
}
void vbe_blit_rect(int x, int y, int w, int h, uint8_t* src_buf, uint32_t src_stride) {
    (void)x; (void)y; (void)w; (void)h; (void)src_buf; (void)src_stride;
}
uint32_t vbe_get_width(void) { return fb_width; }
uint32_t vbe_get_height(void) { return fb_height; }
uint32_t vbe_get_bpp(void) { return fb_bpp; }

void screen_set_graphics_enabled(int enabled) {
    graphics_enabled = enabled != 0 && framebuffer != 0;
}

int screen_is_graphics_enabled(void) {
    return graphics_enabled;
}

void clear_screen(void) {
    vbe_clear(0x101820);
}

void vga_putchar(char c) {
    serial_write_char(c);
}

void vga_backspace(void) {
    serial_write("\b \b");
}

void vga_newline(void) {
    serial_write_char('\n');
}

void vga_print(const char* str) {
    serial_write(str);
}

void vga_println(const char* str) {
    serial_write_line(str);
}

void vga_print_color(const char* str, uint8_t color) {
    (void)color;
    serial_write(str);
}

void vga_print_int(int num) {
    uint32_t value;

    if (num < 0) {
        serial_write("-");
        value = (uint32_t)(-num);
    } else {
        value = (uint32_t)num;
    }
    display_write_u32_decimal(value);
}

void vga_print_int_hex(uint32_t n, char* buf) {
    static const char hex[] = "0123456789ABCDEF";

    if (!buf) return;
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 8; i++) {
        buf[9 - i] = hex[(n >> (i * 4)) & 0x0FU];
    }
    buf[10] = '\0';
}

void vga_scrollback_page(int direction) { (void)direction; }
void vga_scrollback_home(void) {}
void vga_scrollback_end(void) {}
void vga_scrollback_follow_live(void) {}

int explorer_modal_active(void) { return 0; }
void explorer_cancel_modal(void) {}
void explorer_modal_submit(void) {}
void explorer_modal_backspace(void) {}
void explorer_modal_append_char(char c) { (void)c; }
