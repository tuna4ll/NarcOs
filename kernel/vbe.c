#include <stdint.h>
#include "vbe.h"
#include "string.h"
#include "fs.h"
extern disk_fs_node_t dir_cache[MAX_FILES];
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

void vbe_draw_char(int x, int y, char c, uint32_t color) {
    vbe_draw_char_hd(x, y, c, color, 1);
}

void vbe_draw_int(int x, int y, int num, uint32_t color) {
    char buf[16];
    int pos = 0;
    if (num == 0) buf[pos++] = '0';
    else {
        int n = num;
        if (n < 0) { vbe_draw_char(x, y, '-', color); x += 8; n = -n; }
        while (n > 0) { buf[pos++] = (char)((n % 10) + '0'); n /= 10; }
    }
    for (int i = pos - 1; i >= 0; i--) {
        vbe_draw_char(x, y, buf[i], color);
        x += 8;
    }
}

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
uint16_t snake_icon_bitmap[16] = {
    0b0111110000000000, 0b1111111000000000, 0b1100011000000000, 0b1100011000000000,
    0b1111111000001110, 0b0111110000011111, 0b0000000000110011, 0b0000001111110011,
    0b0000011111100011, 0b0000110000000111, 0b0001110000001110, 0b0001110000011100,
    0b0000111111111000, 0b0000011111100000, 0b0000000000000000, 0b0000000000000000
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

uint32_t vbe_get_pixel(int x, int y) {
    if (x < 0 || (uint32_t)x >= current_target_width || y < 0 || (uint32_t)y >= mode_info->height) return 0;
    uint32_t bpp_bytes = mode_info->bpp / 8;
    int offset = (y * current_target_width + x) * bpp_bytes;
    if (mode_info->bpp == 32) {
        return *(uint32_t*)(current_target + offset);
    } else if (mode_info->bpp == 24) {
        return (current_target[offset + 2] << 16) | (current_target[offset + 1] << 8) | current_target[offset];
    } else if (mode_info->bpp == 16) {
        uint16_t c = *(uint16_t*)(current_target + offset);
        uint32_t r = ((c >> 11) & 0x1F) << 3;
        uint32_t g = ((c >> 5) & 0x3F) << 2;
        uint32_t b = (c & 0x1F) << 3;
        return (r << 16) | (g << 8) | b;
    }
    return 0;
}

uint32_t vbe_mix_color(uint32_t c1, uint32_t c2, int alpha) {
    if (alpha >= 255) return c1;
    if (alpha <= 0) return c2;
    // Fast bitwise alpha blending (approximate: / 256 instead of / 255)
    uint32_t rb1 = c1 & 0xFF00FF;
    uint32_t g1  = c1 & 0x00FF00;
    uint32_t rb2 = c2 & 0xFF00FF;
    uint32_t g2  = c2 & 0x00FF00;

    uint32_t rb = ((rb1 * alpha + rb2 * (256 - alpha)) >> 8) & 0xFF00FF;
    uint32_t g  = ((g1 * alpha + g2 * (256 - alpha)) >> 8) & 0x00FF00;
    
    return rb | g;
}

void vbe_put_pixel_alpha(int x, int y, uint32_t color, int alpha) {
    if (alpha >= 255) { vbe_put_pixel(x, y, color); return; }
    if (alpha <= 0) return;
    uint32_t old_color = vbe_get_pixel(x, y);
    vbe_put_pixel(x, y, vbe_mix_color(color, old_color, alpha));
}

// Anti-Aliased Circle Edge Helper (Linear Falloff)
static int get_aa_alpha(int dx, int dy, int r, int base_alpha) {
    int r2 = r * r;
    int d2 = dx * dx + dy * dy;
    if (d2 <= (r - 1) * (r - 1)) return base_alpha;
    if (d2 > r * r) return 0;
    int diff_r2 = r * r - (r - 1) * (r - 1);
    if (diff_r2 <= 0) return base_alpha;
    int dist_pct = (r2 - d2) * 255 / diff_r2;
    return (base_alpha * dist_pct) >> 8;
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

void vbe_draw_char_hd(int x, int y, char c, uint32_t color, int scale) {
    if (scale <= 1) {
        for (int row = 0; row < 8; row++) {
            for (int col = 0; col < 8; col++) {
                if (vbe_font[(uint8_t)c][row] & (1 << (7 - col))) vbe_put_pixel_alpha(x + col, y + row, color, 255);
            }
        }
        return;
    }

    // Ultra-Smooth Scaling (Sub-pixel Anti-Aliasing)
    for (int row = 0; row < 8 * scale; row++) {
        for (int col = 0; col < 8 * scale; col++) {
            // Find map to original pixel
            int ox = col / scale;
            int oy = row / scale;
            
            // If it's a font pixel
            if (vbe_font[(uint8_t)c][oy] & (1 << (7 - ox))) {
                // Calculate distance to edge for smoothing
                int fx = col % scale;
                int fy = row % scale;
                int edge_dist = 0;
                
                // Simple AA: Pixels near the edges of the scaled block get lower alpha
                if (fx == 0 || fx == scale-1 || fy == 0 || fy == scale-1) edge_dist = 160;
                else edge_dist = 255;
                
                vbe_put_pixel_alpha(x + col, y + row, color, edge_dist);
            }
        }
    }
}

void vbe_draw_string_hd(int x, int y, const char* s, uint32_t color, int scale) {
    int cur_x = x;
    while (*s) {
        vbe_draw_char_hd(cur_x, y, *s++, color, scale);
        cur_x += 8 * scale + 2;
    }
}

void vbe_draw_string(int x, int y, const char* s, uint32_t color) {
    vbe_draw_string_hd(x, y, s, color, 1);
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

// Modern Design Colors
#define COLOR_GLASS_BG     0x1A1A1A
#define COLOR_GLASS_BORDER 0x444444
#define COLOR_ACCENT       0x0078D7
#define COLOR_ACCENT_GLOW  0x00A2FF
#define COLOR_TITLEBAR     0x222222
#define COLOR_TEXT         0xFFFFFF
#define COLOR_TEXT_DIM     0xAAAAAA

void vbe_blit_window(int x, int y, int w, int h, uint8_t* win_buf) {
    // Draw Shadow
    vbe_draw_shadow(x + 5, y + 5, w, h, 12);
    
    // Draw Window Background (Glassmorphism)
    vbe_draw_rounded_rect(x, y, w, h, 12, COLOR_GLASS_BG, 230);
    vbe_draw_rounded_rect(x, y, w, h, 12, COLOR_GLASS_BORDER, 255); // Border
    
    // Titlebar with Gradient
    vbe_fill_rect_gradient(x, y, w, 28, COLOR_TITLEBAR, 0x111111, 1);
    vbe_draw_rounded_rect(x, y, w, 28, 12, COLOR_GLASS_BORDER, 100); // Highlight top edge
    
    // Content Blit (with offset for border/titlebar)
    uint32_t bpp = mode_info->bpp / 8;
    for (int i = 28; i < h - 4; i++) {
        uint8_t* dest = backbuffer + ((y + i) * mode_info->width + (x + 4)) * bpp;
        uint8_t* src  = win_buf + (i * w + 4) * bpp;
        vbe_memcpy_sse(dest, src, (w - 8) * bpp);
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
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)current_target_width) w = current_target_width - x;
    if (y + h > (int)mode_info->height) h = mode_info->height - y;
    if (w <= 0 || h <= 0) return;

    uint32_t bpp = mode_info->bpp / 8;
    for (int i = 0; i < h; i++) {
        uint8_t* p = current_target + ((y + i) * current_target_width + x) * bpp;
        if (bpp == 4) {
            for (int j = 0; j < w; j++) ((uint32_t*)p)[j] = color;
        } else {
            for (int j = 0; j < w; j++) {
                p[j * 3]     = color & 0xFF;
                p[j * 3 + 1] = (color >> 8) & 0xFF;
                p[j * 3 + 2] = (color >> 16) & 0xFF;
            }
        }
    }
}

void vbe_fill_rect_alpha(int x, int y, int w, int h, uint32_t color, int alpha) {
    if (alpha <= 0) return;
    if (alpha >= 255) { vbe_fill_rect(x, y, w, h, color); return; }
    
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)current_target_width) w = current_target_width - x;
    if (y + h > (int)mode_info->height) h = mode_info->height - y;
    if (w <= 0 || h <= 0) return;

    uint32_t bpp = mode_info->bpp / 8;
    for (int i = 0; i < h; i++) {
        uint8_t* p = current_target + ((y + i) * current_target_width + x) * bpp;
        for (int j = 0; j < w; j++) {
            uint32_t old;
            if (bpp == 4) old = *(uint32_t*)p;
            else old = (p[2] << 16) | (p[1] << 8) | p[0];
            
            uint32_t mixed = vbe_mix_color(color, old, alpha);
            
            if (bpp == 4) { *(uint32_t*)p = mixed; p += 4; }
            else { p[0] = mixed & 0xFF; p[1] = (mixed >> 8) & 0xFF; p[2] = (mixed >> 16) & 0xFF; p += 3; }
        }
    }
}

void vbe_draw_rounded_rect(int x, int y, int w, int h, int r, uint32_t color, int alpha) {
    if (r <= 0) { vbe_fill_rect_alpha(x, y, w, h, color, alpha); return; }

    vbe_fill_rect_alpha(x + r, y, w - 2 * r, h, color, alpha);
    vbe_fill_rect_alpha(x, y + r, r, h - 2 * r, color, alpha);
    vbe_fill_rect_alpha(x + w - r, y + r, r, h - 2 * r, color, alpha);

    for (int i = 0; i < r; i++) {
        for (int j = 0; j < r; j++) {
            vbe_put_pixel_alpha(x + j, y + i, color, get_aa_alpha(r - j, r - i, r, alpha)); // TL
            vbe_put_pixel_alpha(x + w - r + j, y + i, color, get_aa_alpha(j + 1, r - i, r, alpha)); // TR
            vbe_put_pixel_alpha(x + j, y + h - r + i, color, get_aa_alpha(r - j, i + 1, r, alpha)); // BL
            vbe_put_pixel_alpha(x + w - r + j, y + h - r + i, color, get_aa_alpha(j + 1, i + 1, r, alpha)); // BR
        }
    }
}

void vbe_draw_shadow(int x, int y, int w, int h, int r) {
    // Highly optimized shadow: Just 3 passes with larger radii
    vbe_draw_rounded_rect(x + 2, y + 2, w + 4, h + 4, r + 2, 0x000000, 30);
    vbe_draw_rounded_rect(x + 4, y + 4, w + 8, h + 8, r + 4, 0x000000, 15);
}
void vbe_draw_taskbar(int start_btn_active) {
    uint32_t w = mode_info->width;
    uint32_t tb_h = 35;
    // Glassy Gradient Taskbar
    vbe_fill_rect_gradient(0, 0, w, tb_h, 0x222222, 0x050505, 1);
    vbe_fill_rect_alpha(0, 0, w, 1, 0x555555, 100); // Top shine
    
    uint32_t btn_color = start_btn_active ? COLOR_ACCENT : 0x222222;
    vbe_draw_rounded_rect(5, 4, 75, 27, 8, btn_color, 255);
    vbe_draw_string(15, 12, "NARC", COLOR_TEXT);
    
    vbe_draw_rounded_rect(90, 4, 100, 27, 8, 0x333333, 200);
    vbe_draw_string(100, 12, "DEV", COLOR_TEXT_DIM);
}

void vbe_draw_start_menu() {
    vbe_draw_shadow(5, 35, 220, 350, 12);
    vbe_draw_rounded_rect(5, 35, 220, 350, 12, COLOR_GLASS_BG, 240);
    vbe_draw_rounded_rect(5, 35, 220, 350, 12, COLOR_GLASS_BORDER, 255);
    
    vbe_draw_string(20, 50, "APPLICATIONS", COLOR_ACCENT);
    vbe_draw_string(20, 80, "> Terminal", COLOR_TEXT);
    vbe_draw_string(20, 110, "> Snake", COLOR_TEXT);
    vbe_draw_string(20, 140, "> NarcPad", COLOR_TEXT);
    
    vbe_draw_rounded_rect(5, 300, 220, 1, 0, 0x333333, 255);
    vbe_draw_string(20, 320, "SYSTEM INFO", COLOR_ACCENT);
    vbe_draw_string(20, 340, "NarcOs 2.0 Ultra", COLOR_TEXT_DIM);
}
extern uint8_t get_hour();
extern uint8_t get_minute();
extern uint8_t get_second();
void vbe_fill_rect_gradient(int x, int y, int w, int h, uint32_t c1, uint32_t c2, int vertical) {
    (void)vertical; // Suppress unused warning for now
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)current_target_width) w = current_target_width - x;
    if (y + h > (int)mode_info->height) h = mode_info->height - y;
    if (w <= 0 || h <= 0) return;

    for (int i = 0; i < h; i++) {
        int ratio = (i * 255) / h;
        uint32_t color = vbe_mix_color(c2, c1, ratio);
        vbe_fill_rect(x, y + i, w, 1, color);
    }
}

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

void vbe_draw_vector_folder(int x, int y, int selected) {
    uint32_t base_col = selected ? COLOR_ACCENT : 0xFFD700;
    
    // Folder Body
    vbe_draw_rounded_rect(x, y + 6, 36, 26, 4, base_col, 255);
    // Folder Tab (The little sticking out part)
    vbe_draw_rounded_rect(x, y + 2, 14, 8, 3, base_col, 255);
    // Detail / Inner shading
    vbe_fill_rect_alpha(x + 2, y + 10, 32, 1, 0xFFFFFF, 80);
}

void vbe_draw_vector_file(int x, int y, int selected) {
    (void)selected;
    uint32_t base_col = 0xEEEEEE;
    // Main paper
    vbe_draw_rounded_rect(x + 4, y, 28, 36, 2, base_col, 255);
    // Folded corner
    vbe_fill_rect(x + 24, y, 8, 8, 0xCCCCCC);
    // Lines (Content Representation)
    for (int i = 0; i < 4; i++) {
        vbe_fill_rect_alpha(x + 10, y + 14 + i * 5, 16, 1, 0x000000, 40);
    }
}

void vbe_draw_vector_pc(int x, int y) {
    uint32_t silver = 0xB0B0B0;
    // Monitor
    vbe_draw_rounded_rect(x + 2, y, 32, 24, 3, 0x333333, 255);
    vbe_draw_rounded_rect(x + 4, y + 2, 28, 20, 1, 0x0078D7, 200); // Screen GLow
    // Stand
    vbe_fill_rect(x + 16, y + 24, 4, 6, silver);
    vbe_draw_rounded_rect(x + 10, y + 28, 16, 4, 2, silver, 255);
}

void vbe_draw_vector_snake(int x, int y) {
    // A cute AA snake head
    vbe_draw_rounded_rect(x + 4, y + 4, 28, 28, 8, 0x00FF00, 255);
    // Eyes
    vbe_draw_rounded_rect(x + 10, y + 12, 4, 4, 2, 0xFFFFFF, 255);
    vbe_draw_rounded_rect(x + 22, y + 12, 4, 4, 2, 0xFFFFFF, 255);
    // Pupils
    vbe_fill_rect(x + 11, y + 13, 2, 2, 0x000000);
    vbe_fill_rect(x + 23, y + 13, 2, 2, 0x000000);
}

void vbe_draw_icon(int x, int y, int type, const char* label, int selected) {
    if (selected) {
        vbe_draw_rounded_rect(x - 10, y - 8, 56, 65, 10, COLOR_ACCENT, 80);
        vbe_draw_rounded_rect(x - 10, y - 8, 56, 65, 10, COLOR_ACCENT_GLOW, 40);
    }
    
    if (type == 0) vbe_draw_vector_folder(x, y, selected);
    else if (type == 2) vbe_draw_vector_pc(x, y);
    else if (type == 3) vbe_draw_vector_snake(x, y);
    else vbe_draw_vector_file(x, y, selected);

    vbe_draw_string(x - 8, y + 42, label, COLOR_TEXT);
}
#include "fs.h"
extern disk_fs_node_t dir_cache[MAX_FILES];
void vbe_draw_explorer_content(int wx, int wy, int current_dir, int width) {
    int x_off = 24, y_off = 45;
    int col = 0;
    int icons_per_row = (width - 40) / 90;
    for (int i = 0; i < MAX_FILES; i++) {
        if (dir_cache[i].flags != 0 && dir_cache[i].parent_index == current_dir) {
            int type = (dir_cache[i].flags == 2) ? 0 : 1;
            vbe_draw_icon(wx + x_off + col * 90, wy + y_off, type, dir_cache[i].name, 0);
            col++;
            if (col >= icons_per_row) { col = 0; y_off += 85; }
        }
    }
}
void vbe_draw_desktop_icons(int desktop_dir) {
    // Fixed System Icons
    vbe_draw_icon(20, 60, 2, "This PC", 0);
    vbe_draw_icon(20, 300, 3, "Snake", 0);
    
    // Dynamic Desktop Icons from FS
    int x_off = 20, y_off = 140; // Skip PC icon area
    int row = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (dir_cache[i].flags != 0 && dir_cache[i].parent_index == desktop_dir) {
            int type = (dir_cache[i].flags == 2) ? 0 : 1;
            vbe_draw_icon(x_off, y_off + row * 80, type, dir_cache[i].name, 0);
            row++;
            if (row > 5) { row = 0; x_off += 80; } // Wrap if too many
        }
    }
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

void vbe_draw_breadcrumb(int x, int y, int current_dir, int width) {
    vbe_draw_rounded_rect(x + 5, y, width - 10, 24, 6, 0x222222, 180);
    vbe_draw_string(x + 12, y + 5, "Path: /", COLOR_TEXT_DIM);
    if (current_dir != -1) {
        vbe_draw_string(x + 65, y + 5, dir_cache[current_dir].name, COLOR_TEXT);
    }
}
void vbe_draw_narcpad(int x, int y, const char* title, const char* content) {
    vbe_draw_shadow(x + 5, y + 5, 500, 400, 12);
    vbe_draw_rounded_rect(x, y, 500, 400, 12, 0xFCFCFC, 255);
    vbe_draw_rounded_rect(x, y, 500, 400, 12, COLOR_GLASS_BORDER, 255);
    vbe_draw_rounded_rect(x, y, 500, 28, 12, 0xEEEEEE, 255);
    vbe_draw_string(x + 12, y + 8, title, 0x333333);
    vbe_fill_rect(x + 500 - 20, y + 8, 12, 12, 0xFF5555);
    int line_y = y + 45;
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
    
    // Draw Pulsing Caret
    extern uint32_t timer_ticks;
    if ((timer_ticks / 20) % 2 == 0) {
        int caret_x = x + 15 + char_idx * 9;
        vbe_fill_rect(caret_x, line_y, 2, 12, 0x000000);
    }
}
void vbe_draw_snake_game(int x, int y, int* px, int* py, int len, int ax, int ay, int dead, int score, int best) {
    vbe_fill_rect(x, y, 400, 325, 0x111111);
    vbe_draw_rect(x, y, 400, 325, 0x444444);
    vbe_fill_rect(x, y, 400, 25, 0x111111);
    vbe_draw_string(x + 10, y + 5, "Score:", 0x00FF00);
    vbe_draw_int(x + 65, y + 5, score, 0xFFFFFF);
    vbe_draw_string(x + 130, y + 5, "Best:", 0xAAAAAA);
    vbe_draw_int(x + 180, y + 5, best, 0xFFFFFF);
    vbe_fill_rect(x + 400 - 20, y + 5, 12, 12, 0xFF5555);
    if (dead) {
        vbe_draw_string(x + 150, y + 150, "GAME OVER", 0xFF0000);
        vbe_draw_string(x + 130, y + 170, "Press R to Reset", 0xAAAAAA);
    } else {
        vbe_fill_rect(x + ax * 10, y + 35 + ay * 10, 8, 8, 0xFF0000);
        for (int i = 0; i < len; i++) {
            uint32_t col = (i == 0) ? 0x00FF00 : 0x00AA00;
            vbe_fill_rect(x + px[i] * 10, y + 35 + py[i] * 10, 8, 8, col);
        }
    }
}
void vbe_compose_scene(int wx, int wy, int win_vis, int start_vis, int exp_vis, int exp_x, int exp_y, int exp_dir, int pad_vis, int pad_x, int pad_y, const char* pad_title, const char* pad_content, int snk_vis, int snk_x, int snk_y, int* snk_px, int* snk_py, int snk_len, int ax, int ay, int dead, int score, int best, int desktop_dir, int drag_file_idx, int mx, int my, int ctx_vis, int ctx_x, int ctx_y, const char** ctx_items, int ctx_count, int ctx_sel) {
    uint32_t bpp_bytes = mode_info->bpp / 8;
    uint32_t size = mode_info->width * mode_info->height * bpp_bytes;
    vbe_memcpy_sse(composition_buffer, wallpaper_buffer, size);
    uint8_t* old_target = current_target;
    uint32_t old_width = current_target_width;
    vbe_set_target(composition_buffer, mode_info->width);
    vbe_draw_desktop_icons(desktop_dir);
    uint8_t* old_back = backbuffer;
    backbuffer = composition_buffer;
    // HD Scaling: Position windows relatively
    int win_w = (mode_info->width > 1280) ? 800 : 700;
    int win_h = (mode_info->width > 1280) ? 600 : 475;
    
    if (win_vis) vbe_blit_window(wx, wy, win_w, win_h, window_buffer);
    if (exp_vis) {
        int exp_w = (mode_info->width > 1280) ? 800 : 600;
        int exp_h = (mode_info->width > 1280) ? 500 : 400;
        vbe_draw_shadow(exp_x + 5, exp_y + 5, exp_w, exp_h, 12);
        vbe_draw_rounded_rect(exp_x, exp_y, exp_w, exp_h, 12, COLOR_GLASS_BG, 230);
        vbe_draw_rounded_rect(exp_x, exp_y, exp_w, exp_h, 12, COLOR_GLASS_BORDER, 255);
        vbe_draw_rounded_rect(exp_x, exp_y, exp_w, 28, 12, COLOR_TITLEBAR, 240);
        vbe_draw_string(exp_x + 12, exp_y + 8, "NarcExplorer", COLOR_TEXT);
        vbe_fill_rect(exp_x + exp_w - 20, exp_y + 8, 12, 12, 0xFF5555);
        vbe_draw_breadcrumb(exp_x, exp_y + 28, exp_dir, exp_w);
        vbe_draw_explorer_content(exp_x, exp_y + 20, exp_dir, exp_w);
    }
    if (pad_vis) vbe_draw_narcpad(pad_x, pad_y, pad_title, pad_content);
    if (snk_vis) {
        vbe_draw_shadow(snk_x + 5, snk_y + 5, 400, 325, 12);
        vbe_draw_rounded_rect(snk_x, snk_y, 400, 325, 12, 0x000000, 240);
        vbe_draw_rounded_rect(snk_x, snk_y, 400, 325, 12, COLOR_GLASS_BORDER, 255);
        vbe_draw_rounded_rect(snk_x, snk_y, 400, 28, 12, 0x111111, 255);
        vbe_draw_string(snk_x + 12, snk_y + 8, "NarcSnake", COLOR_TEXT);
        vbe_draw_snake_game(snk_x, snk_y, snk_px, snk_py, snk_len, ax, ay, dead, score, best);
    }
    
    // Draw Dragging Ghost Icon
    if (drag_file_idx != -1) {
        int type = (dir_cache[drag_file_idx].flags == 2) ? 0 : 1;
        vbe_draw_icon(mx - 16, my - 16, type, dir_cache[drag_file_idx].name, 1);
    }

    vbe_draw_taskbar(start_vis);
    if (start_vis) vbe_draw_start_menu();
    if (ctx_vis) vbe_draw_context_menu(ctx_x, ctx_y, ctx_items, ctx_count, ctx_sel);
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

void vbe_draw_context_menu(int x, int y, const char** items, int count, int selected_idx) {
    int w = 130;
    int h = count * 22 + 8;
    vbe_draw_shadow(x + 3, y + 3, w, h, 8);
    vbe_draw_rounded_rect(x, y, w, h, 8, COLOR_GLASS_BG, 245);
    vbe_draw_rounded_rect(x, y, w, h, 8, COLOR_GLASS_BORDER, 255);
    
    for (int i = 0; i < count; i++) {
        if (i == selected_idx) {
            vbe_draw_rounded_rect(x + 4, y + 4 + i * 22, w - 8, 20, 6, COLOR_ACCENT, 255);
            vbe_draw_string(x + 12, y + 6 + i * 22, items[i], COLOR_TEXT);
        } else {
            vbe_draw_string(x + 12, y + 6 + i * 22, items[i], COLOR_TEXT);
        }
    }
}
