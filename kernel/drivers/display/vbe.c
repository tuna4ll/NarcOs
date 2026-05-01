#include <stdint.h>
#include "paging.h"
#include "serial.h"
#include "vbe.h"
#include "cpu.h"
#include "string.h"
#include "fs.h"
#include "rtc.h"
#include "net.h"
#include "maple_mono_8x8.h"
#include "usermode.h"
#include "ui_theme.h"

#define UI_GLYPH_W         7
#define UI_GLYPH_ADVANCE   7

extern disk_fs_node_t dir_cache[MAX_FILES];
extern volatile uint32_t timer_ticks;
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

#if defined(__x86_64__)
/*
 * On x86_64, early kernel task stacks live around 12 MiB. A 1920x1080x24bpp
 * backbuffer is ~6.22 MiB, so the old 8 MiB placement was clobbering stacks.
 */
static uint8_t* backbuffer = (uint8_t*)0x3000000;
static uint8_t* wallpaper_buffer = (uint8_t*)0x4000000;
static uint8_t* window_buffer = (uint8_t*)0x5000000;
static uint8_t* composition_buffer = (uint8_t*)0x6000000;
#else
/*
 * On i386 these addresses must remain inside the low identity mapped region;
 * the 8-32 MiB range is safe and does not overlap the kernel stack window.
 */
static uint8_t* backbuffer = (uint8_t*)0x800000;
static uint8_t* wallpaper_buffer = (uint8_t*)0x1000000;
static uint8_t* window_buffer = (uint8_t*)0x1800000;
static uint8_t* composition_buffer = (uint8_t*)0x2000000;
#endif
static uint8_t* framebuffer = 0;
static int wallpaper_init = 0;

volatile int gui_needs_redraw = 1;

static uint8_t* current_target = (uint8_t*)0x800000;
static uint32_t current_target_width = 0;

void vbe_put_pixel_to(uint8_t* buffer, uint32_t buf_width, int x, int y, uint32_t color);
void vbe_put_pixel_alpha(int x, int y, uint32_t color, int alpha);

#if defined(__x86_64__)
extern const uint8_t _binary_obj_x86_64_assets_bg_rgb_start[];
extern const uint8_t _binary_obj_x86_64_assets_bg_rgb_end[];
#else
extern const uint8_t _binary_obj_i386_assets_bg_rgb_start[];
extern const uint8_t _binary_obj_i386_assets_bg_rgb_end[];
#endif

typedef struct {
    const uint8_t* pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
} embedded_ppm_t;

static void vbe_memcpy_local(void* dest, const void* src, uint32_t count) {
    memcpy(dest, src, count);
}

static uint8_t* vbe_frontbuffer(void) {
    if (framebuffer) return framebuffer;
    if (!mode_info->framebuffer) return 0;
    return (uint8_t*)(uintptr_t)mode_info->framebuffer;
}

static void vbe_memcpy_fast(void* dest, const void* src, uint32_t count) {
    vbe_memcpy_local(dest, src, count);
}

static void vbe_memset_fast(void* dest, uint32_t color, uint32_t count_bytes) {
    uint32_t* pixels = (uint32_t*)dest;
    uint32_t count = count_bytes / 4U;
    for (uint32_t i = 0; i < count; i++) {
        pixels[i] = color;
    }
}

static uint32_t vbe_get_pixel_from(uint8_t* buffer, uint32_t buf_width, int x, int y) {
    uint32_t bpp_bytes;
    int offset;

    if (!buffer || x < 0 || y < 0 || (uint32_t)x >= buf_width || (uint32_t)y >= mode_info->height) return 0;
    bpp_bytes = mode_info->bpp / 8;
    offset = (y * (int)buf_width + x) * (int)bpp_bytes;
    if (bpp_bytes == 4U) return *(uint32_t*)(buffer + offset);
    if (bpp_bytes == 3U) return ((uint32_t)buffer[offset + 2] << 16) |
                                  ((uint32_t)buffer[offset + 1] << 8) |
                                  (uint32_t)buffer[offset];
    return 0;
}

static int load_embedded_bg(embedded_ppm_t* out) {
    const uint8_t* start;
    const uint8_t* end;
    uint32_t width;
    uint32_t height;
    uint32_t expected_size;

    if (!out) return 0;
#if defined(__x86_64__)
    start = _binary_obj_x86_64_assets_bg_rgb_start;
    end = _binary_obj_x86_64_assets_bg_rgb_end;
    width = 400U;
    height = 225U;
#else
    start = _binary_obj_i386_assets_bg_rgb_start;
    end = _binary_obj_i386_assets_bg_rgb_end;
    width = 200U;
    height = 112U;
#endif
    expected_size = width * height * 3U;
    if (!start || !end || end <= start) return 0;
    if ((uint32_t)(end - start) < expected_size) return 0;

    out->pixels = start;
    out->width = width;
    out->height = height;
    out->stride = width * 3U;
    return 1;
}

static void wallpaper_draw_fallback_gradient(void) {
    for (uint32_t y = 0; y < mode_info->height; y++) {
        for (uint32_t x = 0; x < mode_info->width; x++) {
            int ratio = (int)((y * 255U) / mode_info->height);
            uint32_t color = vbe_mix_color(UI_DESKTOP_BOTTOM, UI_DESKTOP_TOP, 255 - ratio);
            if (((x + y) % 47U) == 0) {
                color = vbe_mix_color(UI_ACCENT_DEEP, color, 36);
            } else if (((x * 3U + y) % 131U) == 0) {
                color = vbe_mix_color(UI_ACCENT_ALT, color, 18);
            } else if (y > mode_info->height / 2 && ((x + y * 2U) % 173U) < 2U) {
                color = vbe_mix_color(UI_DESKTOP_GLOW, color, 22);
            }
            vbe_put_pixel_to(wallpaper_buffer, mode_info->width, x, y, color);
        }
    }

    {
        int glow_w = (int)mode_info->width / 3;
        int glow_h = (int)mode_info->height / 2;
        int glow_x = (int)mode_info->width - glow_w - 42;
        int glow_y = 42;

        for (int gy = 0; gy < glow_h; gy++) {
            for (int gx = 0; gx < glow_w; gx++) {
                int dx = gx - glow_w / 2;
                int dy = gy - glow_h / 2;
                int dist = (dx * dx) / (glow_w / 2 + 1) + (dy * dy) / (glow_h / 2 + 1);
                int alpha = 100 - dist;
                if (alpha > 0) {
                    uint32_t old_color = vbe_get_pixel_from(wallpaper_buffer, mode_info->width, glow_x + gx, glow_y + gy);
                    vbe_put_pixel_to(wallpaper_buffer, mode_info->width, glow_x + gx, glow_y + gy,
                                     vbe_mix_color(UI_DESKTOP_GLOW, old_color, alpha));
                }
            }
        }
    }
}

static uint32_t wallpaper_sample_bilinear_rgb(const embedded_ppm_t* image, uint32_t x_fp, uint32_t y_fp) {
    uint32_t x0 = x_fp >> 16;
    uint32_t y0 = y_fp >> 16;
    uint32_t fx = x_fp & 0xFFFFU;
    uint32_t fy = y_fp & 0xFFFFU;
    uint32_t x1 = (x0 + 1U < image->width) ? x0 + 1U : x0;
    uint32_t y1 = (y0 + 1U < image->height) ? y0 + 1U : y0;
    const uint8_t* p00 = image->pixels + y0 * image->stride + x0 * 3U;
    const uint8_t* p10 = image->pixels + y0 * image->stride + x1 * 3U;
    const uint8_t* p01 = image->pixels + y1 * image->stride + x0 * 3U;
    const uint8_t* p11 = image->pixels + y1 * image->stride + x1 * 3U;
    uint32_t inv_fx = 65536U - fx;
    uint32_t inv_fy = 65536U - fy;
    uint64_t w00 = (uint64_t)inv_fx * (uint64_t)inv_fy;
    uint64_t w10 = (uint64_t)fx * (uint64_t)inv_fy;
    uint64_t w01 = (uint64_t)inv_fx * (uint64_t)fy;
    uint64_t w11 = (uint64_t)fx * (uint64_t)fy;
    uint32_t r = (uint32_t)((p00[0] * w00 + p10[0] * w10 + p01[0] * w01 + p11[0] * w11) >> 32);
    uint32_t g = (uint32_t)((p00[1] * w00 + p10[1] * w10 + p01[1] * w01 + p11[1] * w11) >> 32);
    uint32_t b = (uint32_t)((p00[2] * w00 + p10[2] * w10 + p01[2] * w01 + p11[2] * w11) >> 32);
    return (r << 16) | (g << 8) | b;
}

static int wallpaper_draw_embedded_image(void) {
    embedded_ppm_t image;
    uint32_t src_w;
    uint32_t src_h;
    uint32_t dst_w;
    uint32_t dst_h;
    uint32_t x_step;
    uint32_t y_step;

    if (!load_embedded_bg(&image)) return 0;
    src_w = image.width;
    src_h = image.height;
    dst_w = mode_info->width;
    dst_h = mode_info->height;
    if (src_w == 0U || src_h == 0U || dst_w == 0U || dst_h == 0U) return 0;

    x_step = (dst_w > 1U) ? (((src_w - 1U) << 16) / (dst_w - 1U)) : 0U;
    y_step = (dst_h > 1U) ? (((src_h - 1U) << 16) / (dst_h - 1U)) : 0U;
    if (x_step == 0U) x_step = 1U;
    if (y_step == 0U) y_step = 1U;

    for (uint32_t y = 0; y < dst_h; y++) {
        uint32_t sy_fp = y * y_step;
        uint32_t max_y_fp = (image.height - 1U) << 16;
        if (sy_fp > max_y_fp) sy_fp = max_y_fp;
        for (uint32_t x = 0; x < dst_w; x++) {
            uint32_t sx_fp = x * x_step;
            uint32_t max_x_fp = (image.width - 1U) << 16;
            uint32_t color;
            if (sx_fp > max_x_fp) sx_fp = max_x_fp;
            color = wallpaper_sample_bilinear_rgb(&image, sx_fp, sy_fp);
            vbe_put_pixel_to(wallpaper_buffer, mode_info->width, (int)x, (int)y, color);
        }
    }
    return 1;
}

static void vbe_alpha_blend_fast(void* dest, uint32_t color, uint32_t alpha, uint32_t count_pixels) {
    uint32_t* pixels = (uint32_t*)dest;
    for (uint32_t i = 0; i < count_pixels; i++) {
        pixels[i] = vbe_mix_color(color, pixels[i], (int)alpha);
    }
}

static void vbe_draw_glyph_solid_32(int x, int y, const unsigned char* glyph, uint32_t color) {
    int start_col = 0;
    int end_col = UI_GLYPH_W;
    int start_row = 0;
    int end_row = 8;

    if (x < 0) start_col = -x;
    if (y < 0) start_row = -y;
    if (x + end_col > (int)current_target_width) end_col = (int)current_target_width - x;
    if (y + end_row > (int)mode_info->height) end_row = (int)mode_info->height - y;
    if (start_col >= end_col || start_row >= end_row) return;

    for (int row = start_row; row < end_row; row++) {
        uint32_t* dest = (uint32_t*)(current_target + ((y + row) * current_target_width + x + start_col) * 4U);
        unsigned char bits = glyph[row];
        for (int col = start_col; col < end_col; col++) {
            if ((bits & (1U << (7 - col))) != 0U) {
                dest[col - start_col] = color;
            }
        }
    }
}

enum {
    GLYPH_C_CEDILLA_UPPER = 256,
    GLYPH_G_BREVE_UPPER,
    GLYPH_I_DOTTED_UPPER,
    GLYPH_O_UMLAUT_UPPER,
    GLYPH_S_CEDILLA_UPPER,
    GLYPH_U_UMLAUT_UPPER,
    GLYPH_C_CEDILLA_LOWER,
    GLYPH_G_BREVE_LOWER,
    GLYPH_DOTLESS_I_LOWER,
    GLYPH_O_UMLAUT_LOWER,
    GLYPH_S_CEDILLA_LOWER,
    GLYPH_U_UMLAUT_LOWER
};

static unsigned char glyph_c_cedilla_upper[8] = {0x00, 0x70, 0xC0, 0x80, 0x80, 0x80, 0xC0, 0x70};
static unsigned char glyph_g_breve_upper[8]   = {0x30, 0x70, 0x90, 0x80, 0xB0, 0x90, 0x90, 0x70};
static unsigned char glyph_i_dotted_upper[8]  = {0x20, 0x70, 0x20, 0x20, 0x20, 0x20, 0x20, 0x70};
static unsigned char glyph_o_umlaut_upper[8]  = {0x50, 0x60, 0x90, 0x90, 0x90, 0x90, 0x90, 0x60};
static unsigned char glyph_s_cedilla_upper[8] = {0x00, 0x70, 0x48, 0x60, 0x38, 0x08, 0x48, 0x78};
static unsigned char glyph_u_umlaut_upper[8]  = {0x50, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x30};
static unsigned char glyph_c_cedilla_lower[8] = {0x00, 0x00, 0x00, 0x38, 0x40, 0x40, 0x40, 0x38};
static unsigned char glyph_g_breve_lower[8]   = {0x00, 0x70, 0x00, 0x38, 0x48, 0x48, 0x48, 0x78};
static unsigned char glyph_dotless_i_lower[8] = {0x00, 0x00, 0x00, 0x60, 0x20, 0x20, 0x20, 0xF8};
static unsigned char glyph_o_umlaut_lower[8]  = {0x00, 0x50, 0x00, 0x30, 0x48, 0x48, 0x48, 0x30};
static unsigned char glyph_s_cedilla_lower[8] = {0x00, 0x00, 0x00, 0x78, 0x40, 0x30, 0x48, 0x78};
static unsigned char glyph_u_umlaut_lower[8]  = {0x00, 0x50, 0x00, 0x48, 0x48, 0x48, 0x48, 0x78};

static unsigned char* vbe_get_glyph_bitmap(uint16_t glyph) {
    switch (glyph) {
        case GLYPH_C_CEDILLA_UPPER: return glyph_c_cedilla_upper;
        case GLYPH_G_BREVE_UPPER: return glyph_g_breve_upper;
        case GLYPH_I_DOTTED_UPPER: return glyph_i_dotted_upper;
        case GLYPH_O_UMLAUT_UPPER: return glyph_o_umlaut_upper;
        case GLYPH_S_CEDILLA_UPPER: return glyph_s_cedilla_upper;
        case GLYPH_U_UMLAUT_UPPER: return glyph_u_umlaut_upper;
        case GLYPH_C_CEDILLA_LOWER: return glyph_c_cedilla_lower;
        case GLYPH_G_BREVE_LOWER: return glyph_g_breve_lower;
        case GLYPH_DOTLESS_I_LOWER: return glyph_dotless_i_lower;
        case GLYPH_O_UMLAUT_LOWER: return glyph_o_umlaut_lower;
        case GLYPH_S_CEDILLA_LOWER: return glyph_s_cedilla_lower;
        case GLYPH_U_UMLAUT_LOWER: return glyph_u_umlaut_lower;
        default: return vbe_font[glyph & 0xFF];
    }
}

static uint16_t vbe_map_codepoint(uint32_t cp) {
    switch (cp) {
        case 0x00C7: return GLYPH_C_CEDILLA_UPPER;
        case 0x011E: return GLYPH_G_BREVE_UPPER;
        case 0x0130: return GLYPH_I_DOTTED_UPPER;
        case 0x00D6: return GLYPH_O_UMLAUT_UPPER;
        case 0x015E: return GLYPH_S_CEDILLA_UPPER;
        case 0x00DC: return GLYPH_U_UMLAUT_UPPER;
        case 0x00E7: return GLYPH_C_CEDILLA_LOWER;
        case 0x011F: return GLYPH_G_BREVE_LOWER;
        case 0x0131: return GLYPH_DOTLESS_I_LOWER;
        case 0x00F6: return GLYPH_O_UMLAUT_LOWER;
        case 0x015F: return GLYPH_S_CEDILLA_LOWER;
        case 0x00FC: return GLYPH_U_UMLAUT_LOWER;
        default:
            if (cp < 256) return (uint16_t)cp;
            return '?';
    }
}

static uint16_t vbe_utf8_next_glyph(const char** s) {
    const unsigned char* p = (const unsigned char*)*s;
    uint32_t cp;
    if (*p == 0) return 0;
    if (*p < 0x80) {
        (*s)++;
        return (uint16_t)(*p);
    }
    if ((p[0] & 0xE0) == 0xC0 && p[1] != 0) {
        cp = ((uint32_t)(p[0] & 0x1F) << 6) | (uint32_t)(p[1] & 0x3F);
        *s += 2;
        return vbe_map_codepoint(cp);
    }
    if ((p[0] & 0xF0) == 0xE0 && p[1] != 0 && p[2] != 0) {
        cp = ((uint32_t)(p[0] & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (uint32_t)(p[2] & 0x3F);
        *s += 3;
        return vbe_map_codepoint(cp);
    }
    (*s)++;
    return '?';
}

void vbe_draw_char(int x, int y, char c, uint32_t color) {
    vbe_draw_char_hd(x, y, c, color, 1);
}

void vbe_draw_int(int x, int y, int num, uint32_t color) {
    char buf[16];
    int pos = 0;
    if (num == 0) buf[pos++] = '0';
    else {
        int n = num;
        if (n < 0) { vbe_draw_char(x, y, '-', color); x += UI_GLYPH_ADVANCE; n = -n; }
        while (n > 0) { buf[pos++] = (char)((n % 10) + '0'); n /= 10; }
    }
    for (int i = pos - 1; i >= 0; i--) {
        vbe_draw_char(x, y, buf[i], color);
        x += UI_GLYPH_ADVANCE;
    }
}

uint16_t mouse_cursor_bitmap[12] = {
    0b110000000000, 0b111000000000, 0b111100000000, 0b111110000000,
    0b111111000000, 0b111111100000, 0b111111110000, 0b111111111000,
    0b111111111100, 0b111111000000, 0b110111000000, 0b100011000000
};

static cursor_mode_t current_cursor_mode = CURSOR_MODE_ARROW;

void vbe_set_cursor_mode(cursor_mode_t mode) {
    current_cursor_mode = mode;
}

static void vbe_draw_cursor_pixel(int x, int y, uint32_t color) {
    if (mode_info->bpp == 32) vbe_put_pixel_alpha(x, y, color, 255);
    else vbe_put_pixel(x, y, color);
}

static void vbe_draw_cursor_hresize(int x, int y) {
    for (int px = 3; px <= 8; px++) vbe_draw_cursor_pixel(x + px, y + 5, 0xFFFFFF);
    vbe_draw_cursor_pixel(x + 2, y + 5, 0xFFFFFF);
    vbe_draw_cursor_pixel(x + 3, y + 4, 0xFFFFFF);
    vbe_draw_cursor_pixel(x + 3, y + 6, 0xFFFFFF);
    vbe_draw_cursor_pixel(x + 9, y + 5, 0xFFFFFF);
    vbe_draw_cursor_pixel(x + 8, y + 4, 0xFFFFFF);
    vbe_draw_cursor_pixel(x + 8, y + 6, 0xFFFFFF);
}

static void vbe_draw_cursor_vresize(int x, int y) {
    for (int py = 3; py <= 8; py++) vbe_draw_cursor_pixel(x + 5, y + py, 0xFFFFFF);
    vbe_draw_cursor_pixel(x + 5, y + 2, 0xFFFFFF);
    vbe_draw_cursor_pixel(x + 4, y + 3, 0xFFFFFF);
    vbe_draw_cursor_pixel(x + 6, y + 3, 0xFFFFFF);
    vbe_draw_cursor_pixel(x + 5, y + 9, 0xFFFFFF);
    vbe_draw_cursor_pixel(x + 4, y + 8, 0xFFFFFF);
    vbe_draw_cursor_pixel(x + 6, y + 8, 0xFFFFFF);
}

static void vbe_draw_cursor_diag_lr(int x, int y) {
    for (int i = 2; i <= 8; i++) vbe_draw_cursor_pixel(x + i, y + i, 0xFFFFFF);
    vbe_draw_cursor_pixel(x + 2, y + 3, 0xFFFFFF);
    vbe_draw_cursor_pixel(x + 3, y + 2, 0xFFFFFF);
    vbe_draw_cursor_pixel(x + 7, y + 8, 0xFFFFFF);
    vbe_draw_cursor_pixel(x + 8, y + 7, 0xFFFFFF);
}

static void vbe_draw_cursor_diag_rl(int x, int y) {
    for (int i = 2; i <= 8; i++) vbe_draw_cursor_pixel(x + (10 - i), y + i, 0xFFFFFF);
    vbe_draw_cursor_pixel(x + 8, y + 3, 0xFFFFFF);
    vbe_draw_cursor_pixel(x + 7, y + 2, 0xFFFFFF);
    vbe_draw_cursor_pixel(x + 3, y + 8, 0xFFFFFF);
    vbe_draw_cursor_pixel(x + 2, y + 7, 0xFFFFFF);
}

void* vbe_get_backbuffer() { return backbuffer; }

void vbe_update() {
    uint8_t* frontbuffer = vbe_frontbuffer();

    if (!frontbuffer) return;
    uint32_t bpp_bytes = mode_info->bpp / 8;
    uint32_t row_size = mode_info->width * bpp_bytes;
    if (mode_info->pitch == row_size) {
        vbe_memcpy_fast(frontbuffer, backbuffer, mode_info->width * mode_info->height * bpp_bytes);
    } else {
        for(uint32_t y = 0; y < mode_info->height; y++) {
            vbe_memcpy_fast(frontbuffer + y * mode_info->pitch, backbuffer + y * row_size, row_size);
        }
    }
}

void vbe_present_composition_with_cursor(int mx, int my) {
    uint8_t* saved_backbuffer = backbuffer;

    backbuffer = composition_buffer;
    vbe_update();
    backbuffer = saved_backbuffer;
    vbe_render_mouse_direct(mx, my);
}

void vbe_present_composition_region(int x, int y, int w, int h) {
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > (int)mode_info->width) w = (int)mode_info->width - x;
    if (y + h > (int)mode_info->height) h = (int)mode_info->height - y;
    if (w <= 0 || h <= 0) return;
    vbe_blit_rect(x, y, w, h, composition_buffer, mode_info->width);
}

void wait_vsync() {
    while (inb(0x3DA) & 8);
    while (!(inb(0x3DA) & 8));
}

void init_vbe() {
    size_t framebuffer_size;

    framebuffer = 0;
    serial_write("[vbe] mode w=");
    serial_write_hex32(mode_info->width);
    serial_write(" h=");
    serial_write_hex32(mode_info->height);
    serial_write(" pitch=");
    serial_write_hex32(mode_info->pitch);
    serial_write(" bpp=");
    serial_write_hex32(mode_info->bpp);
    serial_write(" fb=");
    serial_write_hex32(mode_info->framebuffer);
    serial_write_char('\n');
    if (mode_info->framebuffer && mode_info->width != 0 && mode_info->height != 0 && mode_info->pitch != 0) {
        framebuffer_size = (size_t)mode_info->pitch * (size_t)mode_info->height;
        framebuffer = (uint8_t*)paging_map_physical((uintptr_t)mode_info->framebuffer, framebuffer_size,
                                                    PAGING_FLAG_WRITE | PAGING_FLAG_CACHE_DISABLE);
    }
    current_target = backbuffer;
    current_target_width = mode_info->width;
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
        if (!wallpaper_draw_embedded_image()) wallpaper_draw_fallback_gradient();
        wallpaper_init = 1;
    }
    vbe_memcpy_fast(backbuffer, wallpaper_buffer, screen_size);
}

void vbe_draw_cursor(int x, int y) {
    if (current_cursor_mode == CURSOR_MODE_RESIZE_H) {
        vbe_draw_cursor_hresize(x, y);
        return;
    }
    if (current_cursor_mode == CURSOR_MODE_RESIZE_V) {
        vbe_draw_cursor_vresize(x, y);
        return;
    }
    if (current_cursor_mode == CURSOR_MODE_RESIZE_DIAG_LR) {
        vbe_draw_cursor_diag_lr(x, y);
        return;
    }
    if (current_cursor_mode == CURSOR_MODE_RESIZE_DIAG_RL) {
        vbe_draw_cursor_diag_rl(x, y);
        return;
    }
    if (mode_info->bpp == 32) {
        int start_x = x < 0 ? 0 : x;
        int start_y = y < 0 ? 0 : y;
        int end_x = x + 12;
        int end_y = y + 12;

        if (end_x > (int)current_target_width) end_x = (int)current_target_width;
        if (end_y > (int)mode_info->height) end_y = (int)mode_info->height;
        if (start_x >= end_x || start_y >= end_y) return;

        for (int row = start_y; row < end_y; row++) {
            int cursor_row = row - y;
            uint16_t bits = mouse_cursor_bitmap[cursor_row];
            uint32_t* dest = (uint32_t*)(current_target + (row * current_target_width + start_x) * 4U);
            for (int col = start_x; col < end_x; col++) {
                int cursor_col = col - x;
                if ((bits & (1U << (11 - cursor_col))) != 0U) {
                    dest[col - start_x] = 0xFFFFFF;
                }
            }
        }
        return;
    }

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
    unsigned char* glyph = vbe_get_glyph_bitmap((uint8_t)c);
    if (scale <= 1) {
        if (mode_info->bpp == 32) {
            vbe_draw_glyph_solid_32(x, y, glyph, color);
            return;
        }
        for (int row = 0; row < 8; row++) {
            for (int col = 0; col < UI_GLYPH_W; col++) {
                if (glyph[row] & (1 << (7 - col))) vbe_put_pixel_alpha(x + col, y + row, color, 255);
            }
        }
        return;
    }

    for (int row = 0; row < 8 * scale; row++) {
        for (int col = 0; col < UI_GLYPH_W * scale; col++) {
            int ox = col / scale;
            int oy = row / scale;
            
            if (glyph[oy] & (1 << (7 - ox))) {
                int fx = col % scale;
                int fy = row % scale;
                int edge_dist = 0;
                
                if (fx == 0 || fx == scale-1 || fy == 0 || fy == scale-1) edge_dist = 160;
                else edge_dist = 255;
                
                vbe_put_pixel_alpha(x + col, y + row, color, edge_dist);
            }
        }
    }
}

static void vbe_draw_glyph_hd(int x, int y, uint16_t glyph_id, uint32_t color, int scale) {
    unsigned char* glyph = vbe_get_glyph_bitmap(glyph_id);
    if (scale <= 1) {
        if (mode_info->bpp == 32) {
            vbe_draw_glyph_solid_32(x, y, glyph, color);
            return;
        }
        for (int row = 0; row < 8; row++) {
            for (int col = 0; col < UI_GLYPH_W; col++) {
                if (glyph[row] & (1 << (7 - col))) vbe_put_pixel_alpha(x + col, y + row, color, 255);
            }
        }
        return;
    }
    for (int row = 0; row < 8 * scale; row++) {
        for (int col = 0; col < UI_GLYPH_W * scale; col++) {
            int ox = col / scale;
            int oy = row / scale;
            if (glyph[oy] & (1 << (7 - ox))) {
                int fx = col % scale;
                int fy = row % scale;
                int edge_dist = (fx == 0 || fx == scale - 1 || fy == 0 || fy == scale - 1) ? 160 : 255;
                vbe_put_pixel_alpha(x + col, y + row, color, edge_dist);
            }
        }
    }
}

void vbe_draw_string_hd(int x, int y, const char* s, uint32_t color, int scale) {
    int cur_x = x;
    while (*s) {
        uint16_t glyph = vbe_utf8_next_glyph(&s);
        if (glyph == 0) break;
        vbe_draw_glyph_hd(cur_x, y, glyph, color, scale);
        cur_x += UI_GLYPH_ADVANCE * scale;
    }
}

void vbe_draw_string(int x, int y, const char* s, uint32_t color) {
    vbe_draw_string_hd(x, y, s, color, 1);
}

void vbe_render_mouse(int x, int y) { vbe_draw_cursor(x, y); }

void vbe_render_mouse_direct(int x, int y) {
    uint8_t* frontbuffer = vbe_frontbuffer();
    uint8_t* old_target = current_target;
    uint32_t old_width = current_target_width;

    if (!frontbuffer) return;
    current_target = frontbuffer;
    current_target_width = mode_info->width;
    vbe_draw_cursor(x, y);
    current_target = old_target;
    current_target_width = old_width;
}
void vbe_copy_to_buffer(void* source) {
    uint32_t size = mode_info->width * mode_info->height * (mode_info->bpp / 8);
    vbe_memcpy_fast(backbuffer, source, size);
}

#define COLOR_GLASS_BG     UI_SURFACE_1
#define COLOR_GLASS_BORDER UI_BORDER_SOFT
#define COLOR_ACCENT       UI_ACCENT
#define COLOR_ACCENT_GLOW  UI_ACCENT_ALT
#define COLOR_TITLEBAR     UI_SURFACE_2
#define COLOR_TEXT         UI_TEXT
#define COLOR_TEXT_DIM     UI_TEXT_MUTED
#define WINDOW_CLIENT_INSET_X 1
#define WINDOW_CLIENT_TOP 40
#define WINDOW_CLIENT_BOTTOM 8
#define UI_GLYPH_W         7
#define UI_GLYPH_ADVANCE   7

static void ui_draw_panel(int x, int y, int w, int h, int radius, uint32_t fill, int fill_alpha, uint32_t border, int border_alpha) {
    vbe_draw_shadow(x + 1, y + 2, w, h, radius);
    vbe_draw_rounded_rect(x, y, w, h, radius, fill, fill_alpha);
    if (w > 6 && h > 6) {
        vbe_draw_rounded_rect(x + 1, y + 1, w - 2, h - 2, radius > 1 ? radius - 1 : radius, 0xFFFFFF, 8);
    }
    vbe_draw_rounded_rect(x, y, w, h, radius, border, border_alpha);
}

static void ui_draw_panel_flat(int x, int y, int w, int h, int radius, uint32_t fill, int fill_alpha, uint32_t border, int border_alpha) {
    vbe_draw_rounded_rect(x, y, w, h, radius, fill, fill_alpha);
    if (w > 6 && h > 6) {
        vbe_draw_rounded_rect(x + 1, y + 1, w - 2, h - 2, radius > 1 ? radius - 1 : radius, 0xFFFFFF, 5);
    }
    vbe_draw_rounded_rect(x, y, w, h, radius, border, border_alpha);
}

static void ui_draw_chip(int x, int y, int w, int h, uint32_t fill, uint32_t text, const char* label) {
    vbe_draw_rounded_rect(x, y, w, h, UI_RADIUS_SM, fill, 235);
    vbe_draw_rounded_rect(x, y, w, h, UI_RADIUS_SM, UI_BORDER_SOFT, 180);
    if (label) vbe_draw_string(x + 10, y + 6, label, text);
}

static int ui_explorer_sidebar_width(int client_w) {
    if (client_w < 470) return 104;
    if (client_w < 620) return 116;
    return 132;
}

static int ui_settings_compact_layout(int client_w) {
    return client_w < 430;
}

static void ui_draw_modal(void) {
    int sw = (int)mode_info->width;
    int sh = (int)mode_info->height;
    int w = 320;
    int h = 140;
    int x = (sw - w) / 2;
    int y = (sh - h) / 2;
    if (user_explorer_state.modal_mode == USER_EXPLORER_MODAL_NONE) return;
    vbe_fill_rect_alpha(0, 0, sw, sh, 0x02060A, 110);
    ui_draw_panel(x, y, w, h, UI_RADIUS_LG, UI_SURFACE_1, 250, UI_BORDER_STRONG, 255);
    if (user_explorer_state.modal_mode == USER_EXPLORER_MODAL_RENAME) {
        vbe_draw_string(x + 20, y + 20, "Rename Item", UI_TEXT);
        vbe_draw_string(x + 20, y + 38, "Type a new name and press Enter.", UI_TEXT_MUTED);
        vbe_fill_rect_alpha(x + 20, y + 60, w - 40, 28, UI_SURFACE_0, 255);
        vbe_draw_rect(x + 20, y + 60, w - 40, 28, UI_BORDER_SOFT);
        vbe_draw_string(x + 28, y + 69, user_explorer_state.modal_input, UI_TEXT);
        ui_draw_chip(x + w - 136, y + h - 32, 50, 18, UI_SURFACE_2, UI_TEXT_MUTED, "Esc");
        ui_draw_chip(x + w - 78, y + h - 32, 54, 18, UI_ACCENT_DEEP, UI_TEXT, "Enter");
    } else if (user_explorer_state.modal_mode == USER_EXPLORER_MODAL_DELETE) {
        vbe_draw_string(x + 20, y + 20, "Delete Item", UI_DANGER);
        vbe_draw_string(x + 20, y + 42, "Delete the selected item?", UI_TEXT);
        if (user_explorer_state.selected_idx >= 0 && dir_cache[user_explorer_state.selected_idx].flags != 0) {
            vbe_draw_string(x + 20, y + 60, dir_cache[user_explorer_state.selected_idx].name, UI_TEXT_MUTED);
        }
        ui_draw_chip(x + w - 136, y + h - 32, 50, 18, UI_SURFACE_2, UI_TEXT_MUTED, "Esc");
        ui_draw_chip(x + w - 78, y + h - 32, 54, 18, UI_DANGER, UI_TEXT, "Delete");
    }
}

void vbe_get_window_client_rect(window_t* win, int* out_x, int* out_y, int* out_w, int* out_h) {
    if (!win) return;
    if (out_x) *out_x = win->x + WINDOW_CLIENT_INSET_X;
    if (out_y) *out_y = win->y + WINDOW_CLIENT_TOP;
    if (out_w) *out_w = win->w - WINDOW_CLIENT_INSET_X * 2;
    if (out_h) *out_h = win->h - WINDOW_CLIENT_TOP - WINDOW_CLIENT_BOTTOM;
}

static void ui_copy_truncated(char* dst, const char* src, int max_chars) {
    int i = 0;
    if (!dst || max_chars <= 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (src[i] != '\0' && i < max_chars) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void vbe_blit_window(window_t* win, uint8_t* win_buf, int is_focused) {
    if (!win->visible || win->minimized) return;
    
    int x = win->x;
    int y = win->y;
    int w = win->w;
    int h = win->h;

    vbe_draw_shadow(x + 1, y + 2, w, h, UI_RADIUS_LG);
    vbe_draw_rounded_rect(x, y, w, h, UI_RADIUS_LG, COLOR_GLASS_BG, 246);
    vbe_draw_rounded_rect(x, y, w, h, UI_RADIUS_LG, UI_BORDER_STRONG, 220);

    uint32_t title_top = is_focused ? UI_SURFACE_3 : UI_SURFACE_2;
    uint32_t title_bottom = is_focused ? UI_SURFACE_2 : UI_SURFACE_1;
    vbe_fill_rect_gradient(x + 1, y + 1, w - 2, 38, title_top, title_bottom, 1);
    vbe_fill_rect_alpha(x + 1, y + 38, w - 2, 1, is_focused ? UI_ACCENT : UI_BORDER_SOFT, 200);
    vbe_fill_rect_alpha(x + WINDOW_CLIENT_INSET_X, y + WINDOW_CLIENT_TOP,
                        w - WINDOW_CLIENT_INSET_X * 2, h - WINDOW_CLIENT_TOP - WINDOW_CLIENT_BOTTOM,
                        UI_SURFACE_0, 255);

    {
        char title_buf[20];
        int title_chars = (w - 96) / 8;
        if (title_chars < 6) title_chars = 6;
        if (title_chars > 19) title_chars = 19;
        ui_copy_truncated(title_buf, win->title, title_chars);
        vbe_draw_string(x + 18, y + 15, title_buf, COLOR_TEXT);
    }

    vbe_draw_rounded_rect(x + w - 64, y + 14, 8, 8, 4, UI_SUCCESS, 200);
    vbe_draw_rounded_rect(x + w - 46, y + 14, 8, 8, 4, UI_WARNING, 210);
    vbe_draw_rounded_rect(x + w - 28, y + 14, 8, 8, 4, UI_DANGER, 225);
    vbe_fill_rect_alpha(x + w - 18, y + h - 12, 8, 1, UI_TEXT_SUBTLE, 180);
    vbe_fill_rect_alpha(x + w - 15, y + h - 9, 5, 1, UI_TEXT_SUBTLE, 180);
    vbe_fill_rect_alpha(x + w - 12, y + h - 6, 2, 1, UI_TEXT_SUBTLE, 180);

    if (win_buf) {
        uint32_t bpp = mode_info->bpp / 8;
        int screen_w = mode_info->width;
        int screen_h = mode_info->height;

        for (int i = WINDOW_CLIENT_TOP; i < h - WINDOW_CLIENT_BOTTOM; i++) {
            int draw_y = y + i;
            if (draw_y < 0 || draw_y >= screen_h) continue;

            int draw_x = x + WINDOW_CLIENT_INSET_X;
            int copy_w = w - WINDOW_CLIENT_INSET_X * 2;
            int src_off_x = WINDOW_CLIENT_INSET_X;

            if (draw_x < 0) {
                copy_w += draw_x;
                src_off_x -= draw_x;
                draw_x = 0;
            }
            if (draw_x + copy_w > screen_w) {
                copy_w = screen_w - draw_x;
            }

            if (copy_w > 0) {
                uint8_t* dest = backbuffer + (draw_y * screen_w + draw_x) * bpp;
                uint8_t* src  = win_buf + (i * w + src_off_x) * bpp;
                vbe_memcpy_fast(dest, src, copy_w * bpp);
            }
        }
    }
}

void vbe_blit_rect(int x, int y, int w, int h, uint8_t* src_buf, uint32_t src_stride) {
    uint8_t* frontbuffer = vbe_frontbuffer();
    uint32_t bpp = mode_info->bpp / 8;

    if (!frontbuffer) return;
    for (int i = 0; i < h; i++) {
        if (y + i < 0 || (uint32_t)(y + i) >= mode_info->height) continue;
        if (x < 0 || (uint32_t)x >= mode_info->width) continue;
        uint32_t draw_w = (x + w > (int)mode_info->width) ? (mode_info->width - x) : w;
        uint8_t* dest = frontbuffer + ((y + i) * mode_info->pitch + x * bpp);
        uint8_t* src  = src_buf + ((y + i) * src_stride + x) * bpp;
        vbe_memcpy_fast(dest, src, draw_w * bpp);
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
            vbe_memset_fast(p, color, (uint32_t)(w * 4));
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
        if (bpp == 4) {
            vbe_alpha_blend_fast(p, color, (uint32_t)alpha, (uint32_t)w);
        } else {
            for (int j = 0; j < w; j++) {
                uint32_t old = (p[2] << 16) | (p[1] << 8) | p[0];
                uint32_t mixed = vbe_mix_color(color, old, alpha);
                p[0] = mixed & 0xFF; p[1] = (mixed >> 8) & 0xFF; p[2] = (mixed >> 16) & 0xFF; 
                p += 3;
            }
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
    vbe_draw_rounded_rect(x + 1, y + 2, w + 2, h + 4, r + 2, 0x000000, 22);
    vbe_draw_rounded_rect(x + 3, y + 5, w + 6, h + 8, r + 3, 0x000000, 8);
}

static void ui_draw_taskbar_button(int x, int y, int w, int h, int active, const char* label) {
    uint32_t fill = active ? UI_ACCENT_DEEP : UI_TASKBAR_PANEL;
    uint32_t text = active ? UI_TEXT : UI_TEXT_MUTED;
    vbe_draw_rounded_rect(x, y, w, h, 9, fill, 248);
    vbe_draw_rounded_rect(x, y, w, h, 9, active ? UI_ACCENT_ALT : UI_BORDER_SOFT, active ? 210 : 150);
    if (label) vbe_draw_string(x + 28, y + 9, label, text);
    vbe_fill_rect_alpha(x + 12, y + 9, 8, 8, active ? 0xDCEBFA : UI_TEXT_SUBTLE, 235);
}

static void ui_draw_taskbar_terminal_button(int x, int y, int active) {
    uint32_t fill = active ? UI_SURFACE_3 : UI_TASKBAR_PANEL;
    vbe_draw_rounded_rect(x, y, 34, 28, 9, fill, 246);
    vbe_draw_rounded_rect(x, y, 34, 28, 9, active ? UI_ACCENT : UI_BORDER_SOFT, active ? 210 : 150);
    vbe_draw_vector_terminal(x + 2, y - 1);
}

static void ui_draw_taskbar_window_slot(int x, int y, int w, const char* title, int active) {
    uint32_t fill = active ? UI_TASKBAR_PANEL_ALT : UI_TASKBAR_PANEL;
    uint32_t border = active ? UI_ACCENT : UI_BORDER_SOFT;
    uint32_t text = active ? UI_TEXT : UI_TEXT_MUTED;
    vbe_draw_rounded_rect(x, y, w, 26, 8, fill, 244);
    vbe_draw_rounded_rect(x, y, w, 26, 8, border, active ? 205 : 135);
    if (active) vbe_fill_rect_alpha(x + 8, y + 18, 3, 3, UI_ACCENT_ALT, 255);
    vbe_draw_string(x + 16, y + 8, title, text);
}

static void ui_format_rate_short(char* out, uint32_t bytes_per_sec) {
    uint32_t whole;
    uint32_t frac;
    if (!out) return;
    if (bytes_per_sec >= 1024U * 1024U) {
        whole = bytes_per_sec / (1024U * 1024U);
        frac = ((bytes_per_sec % (1024U * 1024U)) * 10U) / (1024U * 1024U);
        out[0] = (char)('0' + (whole / 10U));
        out[1] = (char)('0' + (whole % 10U));
        out[2] = '.';
        out[3] = (char)('0' + frac);
        out[4] = 'M';
        out[5] = '\0';
    } else {
        whole = bytes_per_sec / 1024U;
        if (whole > 99U) whole = 99U;
        out[0] = (char)('0' + (whole / 10U));
        out[1] = (char)('0' + (whole % 10U));
        out[2] = 'K';
        out[3] = '\0';
    }
}

static void ui_draw_taskbar_network_panel(int x, int y, int w, int h) {
    static uint32_t last_sample_tick = 0;
    static uint32_t last_rx_bytes = 0;
    static uint32_t last_tx_bytes = 0;
    static uint16_t rx_hist[24];
    static uint16_t tx_hist[24];
    net_stats_t stats;
    char rx_buf[6];
    char tx_buf[6];
    uint32_t max_rate = 1;

    if (net_get_stats(&stats) != 0 || !stats.available) {
        vbe_draw_rounded_rect(x, y, w, h, 8, UI_TASKBAR_PANEL, 246);
        vbe_draw_rounded_rect(x, y, w, h, 8, UI_BORDER_SOFT, 150);
        vbe_draw_string(x + 10, y + 9, "NET", UI_TEXT_MUTED);
        vbe_draw_string(x + 36, y + 9, "offline", UI_TEXT_SUBTLE);
        return;
    }

    if (last_sample_tick == 0 || timer_ticks - last_sample_tick >= 100U) {
        uint32_t rx_rate = stats.rx_bytes - last_rx_bytes;
        uint32_t tx_rate = stats.tx_bytes - last_tx_bytes;
        for (int i = 0; i < 23; i++) {
            rx_hist[i] = rx_hist[i + 1];
            tx_hist[i] = tx_hist[i + 1];
        }
        rx_hist[23] = (uint16_t)(rx_rate > 65535U ? 65535U : rx_rate);
        tx_hist[23] = (uint16_t)(tx_rate > 65535U ? 65535U : tx_rate);
        last_rx_bytes = stats.rx_bytes;
        last_tx_bytes = stats.tx_bytes;
        last_sample_tick = timer_ticks;
    }

    for (int i = 0; i < 24; i++) {
        if (rx_hist[i] > max_rate) max_rate = rx_hist[i];
        if (tx_hist[i] > max_rate) max_rate = tx_hist[i];
    }

    vbe_draw_rounded_rect(x, y, w, h, 8, UI_TASKBAR_PANEL, 246);
    vbe_draw_rounded_rect(x, y, w, h, 8, UI_BORDER_SOFT, 150);
    vbe_draw_string(x + 8, y + 5, stats.configured ? "NET" : "DHCP", UI_TEXT_MUTED);

    for (int i = 0; i < 24; i++) {
        int bar_h_rx = (rx_hist[i] * 10U) / max_rate;
        int bar_h_tx = (tx_hist[i] * 10U) / max_rate;
        int px = x + 8 + i * 2;
        if (bar_h_rx > 0) vbe_fill_rect_alpha(px, y + 20 - bar_h_rx, 1, bar_h_rx, UI_ACCENT_ALT, 255);
        if (bar_h_tx > 0) vbe_fill_rect_alpha(px + 1, y + 20 - bar_h_tx, 1, bar_h_tx, UI_SUCCESS, 255);
    }

    ui_format_rate_short(rx_buf, rx_hist[23]);
    ui_format_rate_short(tx_buf, tx_hist[23]);
    vbe_draw_string(x + 60, y + 5, rx_buf, UI_ACCENT_ALT);
    vbe_draw_string(x + 60, y + 14, tx_buf, UI_SUCCESS);
}

void vbe_draw_taskbar(int start_btn_active) {
    uint32_t w = mode_info->width;
    uint32_t tb_h = 40;
    int bar_x = 8;
    int bar_y = 6;
    int bar_h = 28;
    int menu_x = bar_x;
    int term_x = menu_x + 100;
    int app_x = term_x + 46;
    int slot_gap = 6;
    int net_w = 102;
    int clock_w = 92;
    int clock_x = (int)w - clock_w - 12;
    int net_x = clock_x - net_w - 8;
    int right_x = net_x - 12;
    int available_w = right_x - app_x - 12;
    int visible_count = 0;
    extern window_t windows[MAX_WINDOWS];
    extern int window_count;
    extern int active_window_idx;

    vbe_fill_rect_alpha(0, 0, w, tb_h, UI_TASKBAR_BG, 236);
    vbe_fill_rect_gradient(0, 0, w, tb_h, UI_TASKBAR_MID, UI_TASKBAR_BG, 1);
    vbe_fill_rect_alpha(0, 0, w, 1, UI_TASKBAR_EDGE, 145);
    vbe_fill_rect_alpha(0, tb_h - 1, w, 1, UI_BORDER_SOFT, 160);
    vbe_fill_rect_alpha(0, tb_h, w, 1, 0x000000, 55);

    ui_draw_taskbar_button(menu_x, bar_y, 94, bar_h, start_btn_active, "NarcOS");
    ui_draw_taskbar_terminal_button(term_x, bar_y, 0);

    for (int i = 0; i < window_count; i++) {
        if (windows[i].visible) visible_count++;
    }

    if (visible_count > 0 && available_w > 90) {
        int slot_w = (available_w - ((visible_count - 1) * slot_gap)) / visible_count;
        if (slot_w > 118) slot_w = 118;
        if (slot_w < 76) slot_w = 76;
        int strip_w = visible_count * slot_w + (visible_count - 1) * slot_gap;
        int strip_x = app_x + (available_w - strip_w) / 2;
        int current_slot_x = strip_x;

        for (int i = 0; i < window_count; i++) {
            char title_buf[13];
            int title_chars;
            if (!windows[i].visible) continue;
            title_chars = (slot_w - 24) / 7;
            if (title_chars < 6) title_chars = 6;
            if (title_chars > 12) title_chars = 12;
            ui_copy_truncated(title_buf, windows[i].title, title_chars);
            ui_draw_taskbar_window_slot(current_slot_x, bar_y + 1, slot_w, title_buf, i == active_window_idx);
            current_slot_x += slot_w + slot_gap;
        }
    }

    ui_draw_taskbar_network_panel(net_x, bar_y, net_w, 28);
}

void vbe_draw_start_menu() {
    ui_draw_panel(10, 42, 276, 324, UI_RADIUS_LG, UI_SURFACE_1, 246, UI_BORDER_SOFT, 255);
    vbe_draw_string(26, 62, "Applications", UI_TEXT);
    vbe_draw_string(26, 80, "Quick launch", UI_TEXT_SUBTLE);

    ui_draw_chip(22, 106, 116, 26, UI_SURFACE_2, UI_TEXT, "Terminal");
    ui_draw_chip(148, 106, 116, 26, UI_SURFACE_2, UI_TEXT, "Explorer");
    ui_draw_chip(22, 140, 116, 26, UI_SURFACE_2, UI_TEXT, "NarcPad");
    ui_draw_chip(148, 140, 116, 26, UI_SURFACE_2, UI_TEXT, "Settings");
    ui_draw_chip(22, 174, 116, 26, UI_SURFACE_2, UI_TEXT, "Snake");

    ui_draw_panel_flat(22, 222, 242, 84, UI_RADIUS_MD, UI_SURFACE_0, 255, UI_BORDER_SOFT, 255);
    vbe_draw_string(38, 242, "Session", UI_TEXT_SUBTLE);
    vbe_draw_string(38, 260, "NarcOS Desktop", UI_TEXT);
    vbe_draw_string(38, 278, "Local system", UI_TEXT_MUTED);
}
void vbe_fill_rect_gradient(int x, int y, int w, int h, uint32_t c1, uint32_t c2, int vertical) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)current_target_width) w = current_target_width - x;
    if (y + h > (int)mode_info->height) h = mode_info->height - y;
    if (w <= 0 || h <= 0) return;

    if (mode_info->bpp == 32) {
        if (vertical) {
            for (int i = 0; i < h; i++) {
                int ratio = (i * 255) / h;
                uint32_t color = vbe_mix_color(c2, c1, ratio);
                uint32_t* row = (uint32_t*)(current_target + ((y + i) * current_target_width + x) * 4U);
                vbe_memset_fast(row, color, (uint32_t)(w * 4));
            }
        } else {
            for (int iy = 0; iy < h; iy++) {
                uint32_t* row = (uint32_t*)(current_target + ((y + iy) * current_target_width + x) * 4U);
                for (int ix = 0; ix < w; ix++) {
                    int ratio = (ix * 255) / w;
                    row[ix] = vbe_mix_color(c2, c1, ratio);
                }
            }
        }
        return;
    }

    if (vertical) {
        for (int i = 0; i < h; i++) {
            int ratio = (i * 255) / h;
            uint32_t color = vbe_mix_color(c2, c1, ratio);
            vbe_fill_rect(x, y + i, w, 1, color);
        }
    } else {
        for (int i = 0; i < w; i++) {
            int ratio = (i * 255) / w;
            uint32_t color = vbe_mix_color(c2, c1, ratio);
            vbe_fill_rect(x + i, y, 1, h, color);
        }
    }
}

void vbe_draw_clock() {
    uint32_t w = mode_info->width;
    char time_str[9];
    int panel_x = (int)w - 104;
    int panel_y = 6;
    uint8_t hh = get_hour();
    uint8_t mm = get_minute();
    uint8_t ss = get_second();
    time_str[0] = (hh / 10) + '0'; time_str[1] = (hh % 10) + '0'; time_str[2] = ':';
    time_str[3] = (mm / 10) + '0'; time_str[4] = (mm % 10) + '0'; time_str[5] = ':';
    time_str[6] = (ss / 10) + '0'; time_str[7] = (ss % 10) + '0'; time_str[8] = '\0';
    vbe_draw_rounded_rect(panel_x, panel_y, 92, 28, 8, UI_TASKBAR_PANEL, 246);
    vbe_draw_rounded_rect(panel_x, panel_y, 92, 28, 8, UI_BORDER_SOFT, 150);
    vbe_draw_string(panel_x + 14, panel_y + 9, time_str, UI_TEXT);
}

void vbe_draw_vector_folder(int x, int y, int selected) {
    uint32_t base_col = selected ? UI_ACCENT : UI_FOLDER;
    
    vbe_draw_rounded_rect(x, y + 6, 36, 26, 4, base_col, 255);
    vbe_draw_rounded_rect(x, y + 2, 14, 8, 3, base_col, 255);
    vbe_fill_rect_alpha(x + 2, y + 10, 32, 1, 0xFFFFFF, 80);
}

void vbe_draw_vector_file(int x, int y, int selected) {
    uint32_t base_col = selected ? UI_ACCENT_ALT : UI_FILE;
    vbe_draw_rounded_rect(x + 4, y, 28, 36, 2, base_col, 255);
    vbe_fill_rect(x + 24, y, 8, 8, UI_TEXT_MUTED);
    for (int i = 0; i < 4; i++) {
        vbe_fill_rect_alpha(x + 10, y + 14 + i * 5, 16, 1, UI_TEXT_DARK, 40);
    }
}

void vbe_draw_vector_pc(int x, int y) {
    uint32_t silver = 0xB8C7D7;
    vbe_draw_rounded_rect(x + 2, y, 32, 24, 3, UI_SURFACE_2, 255);
    vbe_draw_rounded_rect(x + 4, y + 2, 28, 20, 1, UI_ACCENT_DEEP, 220);
    vbe_fill_rect(x + 16, y + 24, 4, 6, silver);
    vbe_draw_rounded_rect(x + 10, y + 28, 16, 4, 2, silver, 255);
}

void vbe_draw_vector_snake(int x, int y) {
    vbe_draw_rounded_rect(x + 4, y + 4, 28, 28, 8, UI_SUCCESS, 255);
    vbe_draw_rounded_rect(x + 10, y + 12, 4, 4, 2, 0xFFFFFF, 255);
    vbe_draw_rounded_rect(x + 22, y + 12, 4, 4, 2, 0xFFFFFF, 255);
    vbe_fill_rect(x + 11, y + 13, 2, 2, 0x000000);
    vbe_fill_rect(x + 23, y + 13, 2, 2, 0x000000);
}

void vbe_draw_vector_terminal(int x, int y) {
    vbe_draw_rounded_rect(x, y + 4, 30, 24, 4, UI_SURFACE_0, 255);
    vbe_fill_rect_alpha(x + 2, y + 6, 26, 4, UI_SURFACE_3, 255);
    vbe_draw_char(x + 6, y + 12, '>', UI_SUCCESS);
    vbe_draw_char(x + 16, y + 12, '_', UI_SUCCESS);
}

void vbe_draw_icon(int x, int y, int type, const char* label, int selected) {
    if (selected) {
        ui_draw_panel_flat(x - 14, y - 12, 64, 74, UI_RADIUS_MD, UI_SURFACE_2, 210, UI_ACCENT, 180);
    }
    
    if (type == 0) vbe_draw_vector_folder(x, y, selected);
    else if (type == 2) vbe_draw_vector_pc(x, y);
    else if (type == 3) vbe_draw_vector_snake(x, y);
    else vbe_draw_vector_file(x, y, selected);

    vbe_draw_string(x - 8, y + 44, label, selected ? UI_TEXT : UI_TEXT_MUTED);
}
#include "fs.h"
extern disk_fs_node_t dir_cache[MAX_FILES];
extern void vga_refresh_window(void);
extern int vga_window_needs_refresh(void);

static int ui_count_children(int parent_idx) {
    int count = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (dir_cache[i].flags != 0 && dir_cache[i].parent_index == parent_idx) count++;
    }
    return count;
}

static void ui_build_path_for_dir(int dir_idx, char* out, int out_len) {
    int chain[16];
    int count = 0;
    if (!out || out_len <= 0) return;
    if (dir_idx < 0) {
        if (out_len >= 2) {
            out[0] = '/';
            out[1] = '\0';
        }
        return;
    }
    while (dir_idx >= 0 && count < 16) {
        chain[count++] = dir_idx;
        dir_idx = dir_cache[dir_idx].parent_index;
    }
    int pos = 0;
    out[pos++] = '/';
    for (int i = count - 1; i >= 0 && pos < out_len - 1; i--) {
        for (int j = 0; dir_cache[chain[i]].name[j] != '\0' && pos < out_len - 1; j++) {
            out[pos++] = dir_cache[chain[i]].name[j];
        }
        if (i > 0 && pos < out_len - 1) out[pos++] = '/';
    }
    out[pos] = '\0';
}

static void ui_draw_list_card(int x, int y, int w, int h, int type, const char* name, int size, int selected) {
    uint32_t fill = selected ? UI_ACCENT_DEEP : UI_SURFACE_1;
    uint32_t border = selected ? UI_ACCENT_ALT : UI_BORDER_SOFT;
    ui_draw_panel_flat(x, y, w, h, UI_RADIUS_MD, fill, 235, border, 255);
    if (selected) vbe_fill_rect_alpha(x + 8, y + 6, w - 16, 2, UI_ACCENT_ALT, 180);
    if (type == 2) vbe_draw_vector_folder(x + 10, y + 8, selected);
    else vbe_draw_vector_file(x + 10, y + 6, selected);
    vbe_draw_string(x + 56, y + 12, name, UI_TEXT);
    if (type == 2) {
        vbe_draw_string(x + 56, y + 28, "Directory", selected ? UI_TEXT : UI_TEXT_MUTED);
    } else {
        vbe_draw_string(x + 56, y + 28, "File", selected ? UI_TEXT : UI_TEXT_MUTED);
        vbe_draw_int(x + w - 54, y + 28, size, selected ? UI_TEXT : UI_ACCENT_ALT);
        vbe_draw_string(x + w - 28, y + 28, "B", selected ? UI_TEXT_MUTED : UI_TEXT_SUBTLE);
    }
}

static int ui_explorer_visible_rows(int panel_h) {
    int rows = (panel_h - 64) / 54;
    if (rows < 1) rows = 1;
    return rows;
}

void vbe_draw_explorer_content(int x, int y, int w, int h, int current_dir) {
    int compact = w < 620;
    int sidebar_w = ui_explorer_sidebar_width(w);
    int content_x = x + sidebar_w + 12;
    int content_w = w - sidebar_w - 12;
    int panel_y = y;
    int panel_h = h;
    int list_y = panel_y + 12;
    int item_count = ui_count_children(current_dir);
    int content_h = panel_h;
    int visible_rows = ui_explorer_visible_rows(panel_h);
    int max_scroll = item_count > visible_rows ? item_count - visible_rows : 0;
    int start_row;
    int sidebar_chip_w = sidebar_w - 24;
    char selected_buf[28];

    ui_draw_panel_flat(x, panel_y, sidebar_w, panel_h, UI_RADIUS_MD, UI_SURFACE_1, 235, UI_BORDER_SOFT, 255);
    vbe_draw_string(x + 16, panel_y + 16, "Places", UI_TEXT);
    ui_draw_chip(x + 12, panel_y + 40, sidebar_chip_w, 22, current_dir == -1 ? UI_ACCENT_DEEP : UI_SURFACE_2, UI_TEXT, "Root");
    ui_draw_chip(x + 12, panel_y + 68, sidebar_chip_w, 22, UI_SURFACE_2, UI_TEXT, "Desktop");
    ui_draw_chip(x + 12, panel_y + 96, sidebar_chip_w, 22, UI_SURFACE_2, UI_TEXT, compact ? "Home" : "Workspace");
    vbe_fill_rect_alpha(x + 12, panel_y + 132, sidebar_chip_w, 1, UI_BORDER_SOFT, 255);
    vbe_draw_string(x + 16, panel_y + 146, "Items", UI_TEXT_SUBTLE);
    vbe_draw_int(x + 66, panel_y + 146, item_count, UI_ACCENT_ALT);
    vbe_draw_string(x + 16, panel_y + panel_h - 44, "Status", UI_TEXT_SUBTLE);
    if (user_explorer_state.selected_idx >= 0 && dir_cache[user_explorer_state.selected_idx].flags != 0) {
        ui_copy_truncated(selected_buf, dir_cache[user_explorer_state.selected_idx].name, compact ? 11 : 15);
        vbe_draw_string(x + 16, panel_y + panel_h - 28, selected_buf, UI_TEXT);
    } else {
        vbe_draw_string(x + 16, panel_y + panel_h - 28, "No selection", UI_TEXT_MUTED);
    }

    ui_draw_panel_flat(content_x, panel_y, content_w, panel_h, UI_RADIUS_MD, UI_SURFACE_1, 235, UI_BORDER_SOFT, 255);
    vbe_draw_string(content_x + 14, panel_y + 16, "Directory", UI_TEXT);
    vbe_draw_string(content_x + content_w - (compact ? 38 : 72), panel_y + 16, compact ? "Sync" : "Refresh", UI_TEXT_SUBTLE);
    vbe_fill_rect_alpha(content_x + 12, panel_y + 30, content_w - 24, 1, UI_BORDER_SOFT, 255);
    if (user_explorer_state.list_scroll < 0) user_explorer_state.list_scroll = 0;
    if (user_explorer_state.list_scroll > max_scroll) user_explorer_state.list_scroll = max_scroll;
    start_row = user_explorer_state.list_scroll;
    if (item_count == 0) {
        ui_draw_panel_flat(content_x + 16, list_y + 28, content_w - 32, 96, UI_RADIUS_MD, UI_SURFACE_0, 255, UI_BORDER_SOFT, 255);
        vbe_draw_vector_folder(content_x + 34, list_y + 52, 0);
        vbe_draw_string(content_x + 82, list_y + 58, "This directory is empty.", UI_TEXT);
        if (!compact) {
            vbe_draw_string(content_x + 82, list_y + 76, "Create a file or folder to start using this workspace.", UI_TEXT_MUTED);
        } else {
            vbe_draw_string(content_x + 82, list_y + 76, "Create a file or folder to begin.", UI_TEXT_MUTED);
        }
        vbe_fill_rect_alpha(content_x + 12, panel_y + panel_h - 26, content_w - 24, 14, UI_SURFACE_0, 255);
        vbe_draw_string(content_x + 18, panel_y + panel_h - 22, "0 items", UI_TEXT_MUTED);
        return;
    }

    int row = 0;
    int matched_row = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (dir_cache[i].flags != 0 && dir_cache[i].parent_index == current_dir) {
            if (matched_row >= start_row && row < visible_rows) {
                ui_draw_list_card(content_x + 16, list_y + row * 54, content_w - 32, 44,
                                  dir_cache[i].flags, dir_cache[i].name, (int)dir_cache[i].size, user_explorer_state.selected_idx == i);
                row++;
            }
            matched_row++;
            if (row >= visible_rows) break;
        }
    }

    vbe_fill_rect_alpha(content_x + 12, panel_y + content_h - 26, content_w - 24, 14, UI_SURFACE_0, 255);
    vbe_draw_string(content_x + 18, panel_y + content_h - 22, "Ready", UI_TEXT_MUTED);
    vbe_draw_int(content_x + 72, panel_y + content_h - 22, item_count, UI_ACCENT_ALT);
    vbe_draw_string(content_x + 90, panel_y + content_h - 22, "items", UI_TEXT_SUBTLE);
    if (max_scroll > 0) {
        vbe_draw_int(content_x + 132, panel_y + content_h - 22, start_row + 1, UI_TEXT_MUTED);
        vbe_draw_string(content_x + 150, panel_y + content_h - 22, "/", UI_TEXT_SUBTLE);
        vbe_draw_int(content_x + 160, panel_y + content_h - 22, max_scroll + 1, UI_TEXT_MUTED);
    }
    if (content_w > 220 && user_explorer_state.selected_idx >= 0 && dir_cache[user_explorer_state.selected_idx].flags != 0) {
        ui_copy_truncated(selected_buf, dir_cache[user_explorer_state.selected_idx].name, compact ? 10 : 15);
        vbe_draw_string(content_x + content_w - (compact ? 84 : 132), panel_y + content_h - 22, selected_buf, UI_TEXT);
    }
}
void vbe_draw_desktop_icons(int desktop_dir) {
    vbe_draw_icon(20, 60, 2, "This PC", 0);
    vbe_draw_icon(20, 300, 3, "Snake", 0);
    
    // Dynamic Desktop Icons from FS
    int x_off = 20, y_off = 140;
    int row = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (dir_cache[i].flags != 0 && dir_cache[i].parent_index == desktop_dir) {
            int type = (dir_cache[i].flags == 2) ? 0 : 1;
            vbe_draw_icon(x_off, y_off + row * 80, type, dir_cache[i].name, 0);
            row++;
            if (row > 5) { row = 0; x_off += 80; }
        }
    }
}

void vbe_draw_rect(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    vbe_fill_rect(x, y, w, 1, color);
    if (h > 1) vbe_fill_rect(x, y + h - 1, w, 1, color);
    if (h > 2) {
        vbe_fill_rect(x, y + 1, 1, h - 2, color);
        if (w > 1) vbe_fill_rect(x + w - 1, y + 1, 1, h - 2, color);
    }
}

uint32_t vbe_get_width()  { return mode_info->width; }
uint32_t vbe_get_height() { return mode_info->height; }
uint32_t vbe_get_bpp()    { return mode_info->bpp; }

void* vbe_get_window_buffer() { return window_buffer; }

void vbe_draw_breadcrumb(int x, int y, int w, int current_dir) {
    char path_buf[128];
    ui_build_path_for_dir(current_dir, path_buf, sizeof(path_buf));
    ui_draw_panel_flat(x, y, w, 28, UI_RADIUS_SM, UI_SURFACE_1, 235, UI_BORDER_SOFT, 255);
    vbe_draw_string(x + 12, y + 9, path_buf, UI_TEXT);
}
void vbe_draw_narcpad(int x, int y, int w, int h, const char* title, const char* content) {
    (void)title;
    int text_x = x;
    int text_y = y;
    int text_w = w;
    int text_h = h;
    int line_y = text_y + 12;
    int chars_per_line = (text_w - 16) / 8;
    int line_h = 15;
    int visible_lines = (text_h - 24) / line_h;
    int total_lines = 1;
    int current_len = 0;
    int last_line_len = 0;
    int max_top_line;
    int top_line;
    int visual_line = 0;
    char line_buf[192];
    int char_idx = 0;
    const char* scan;
    const char* render;
    if (chars_per_line < 8) chars_per_line = 8;
    if (visible_lines < 1) visible_lines = 1;
    ui_draw_panel_flat(text_x, text_y, text_w, text_h, UI_RADIUS_MD, 0xF5F7FA, 255, 0xD7DEE6, 255);
    for (scan = content; scan && *scan; scan++) {
        if (*scan == '\n') {
            total_lines++;
            current_len = 0;
            last_line_len = 0;
            continue;
        }
        if (current_len < chars_per_line) current_len++;
        last_line_len = current_len;
        if (current_len >= chars_per_line && scan[1] != '\0') {
            total_lines++;
            current_len = 0;
            last_line_len = 0;
        }
    }
    if (current_len > 0) last_line_len = current_len;
    max_top_line = total_lines > visible_lines ? total_lines - visible_lines : 0;
    if (user_narcpad_state.view_scroll < 0) user_narcpad_state.view_scroll = max_top_line;
    if (user_narcpad_state.view_scroll > max_top_line) user_narcpad_state.view_scroll = max_top_line;
    top_line = user_narcpad_state.view_scroll;

    for (render = content; render && *render; render++) {
        if (*render == '\n' || char_idx >= chars_per_line) {
            line_buf[char_idx] = '\0';
            if (visual_line >= top_line && visual_line < top_line + visible_lines) {
                vbe_draw_string(text_x + 8, line_y, line_buf, UI_TEXT_DARK);
                line_y += line_h;
            }
            visual_line++;
            char_idx = 0;
            if (*render == '\n') continue;
        }
        if (char_idx < (int)sizeof(line_buf) - 1) line_buf[char_idx++] = *render;
    }
    line_buf[char_idx] = '\0';
    if (visual_line >= top_line && visual_line < top_line + visible_lines) {
        vbe_draw_string(text_x + 8, line_y, line_buf, UI_TEXT_DARK);
    }
    
    if ((timer_ticks / 20) % 2 == 0) {
        int caret_line = total_lines - 1;
        if (caret_line >= top_line && caret_line < top_line + visible_lines) {
            int caret_x = text_x + 8 + last_line_len * 8;
            int caret_y = text_y + 12 + (caret_line - top_line) * line_h;
            vbe_fill_rect(caret_x, caret_y, 2, 12, UI_ACCENT_DEEP);
        }
    }
    vbe_fill_rect_alpha(text_x + 10, y + h - 18, text_w - 20, 1, 0xD7DEE6, 255);
}
void vbe_draw_snake_game(int x, int y, int w, int h, int* px, int* py, int len, int ax, int ay, int dead, int score, int best) {
    int header_x = x + 14;
    int header_y = y + 14;
    int header_w = w - 28;
    int header_h = 52;
    int grid_w = 39;
    int grid_h = 29;
    int cell_w = (w - 40) / grid_w;
    int cell_h = (h - 92) / grid_h;
    int cell = cell_w < cell_h ? cell_w : cell_h;
    int dot = cell - 2;
    int board_w;
    int board_h;
    int content_top = y + 66;
    int board_x;
    int board_y;
    int game_over_x = x + (w - 84) / 2;
    int reset_text_x = x + (w - 128) / 2;
    if (cell > 9) cell = 9;
    if (cell < 5) cell = 5;
    if (dot < 3) dot = 3;
    board_w = grid_w * cell + 12;
    board_h = grid_h * cell + 12;
    board_x = x + (w - board_w) / 2;
    board_y = content_top + ((h - (content_top - y) - board_h) > 0 ? (h - (content_top - y) - board_h) / 2 : 0);
    if (board_x < x + 8) board_x = x + 8;
    if (board_y < content_top) board_y = content_top;
    ui_draw_panel_flat(x, y, w, h, UI_RADIUS_MD, UI_SURFACE_1, 235, UI_BORDER_SOFT, 255);
    ui_draw_panel_flat(header_x, header_y, header_w, header_h, UI_RADIUS_MD, UI_SURFACE_0, 255, UI_BORDER_SOFT, 255);
    vbe_draw_string(header_x + 14, header_y + 12, "Snake", UI_TEXT);
    vbe_draw_string(header_x + 14, header_y + 30, "Score", UI_TEXT_SUBTLE);
    vbe_draw_int(header_x + 58, header_y + 30, score, UI_TEXT);
    vbe_draw_string(header_x + 118, header_y + 30, "Best", UI_TEXT_SUBTLE);
    vbe_draw_int(header_x + 154, header_y + 30, best, UI_TEXT);
    vbe_draw_string(header_x + header_w - 58, header_y + 30, "R Reset", UI_TEXT_MUTED);

    vbe_fill_rect_alpha(x + 14, header_y + header_h + 2, w - 28, 1, UI_BORDER_SOFT, 255);
    vbe_fill_rect(board_x, board_y, board_w, board_h, 0x10171F);
    vbe_draw_rect(board_x, board_y, board_w, board_h, UI_BORDER_SOFT);
    if (dead) {
        vbe_draw_string(game_over_x, board_y + 112, "GAME OVER", UI_DANGER);
        vbe_draw_string(reset_text_x, board_y + 134, "Press R to Reset", UI_TEXT_MUTED);
    } else {
        vbe_fill_rect(board_x + 6 + ax * cell, board_y + 6 + ay * cell, dot, dot, UI_DANGER);
        for (int i = 0; i < len; i++) {
            uint32_t col = (i == 0) ? UI_SUCCESS : 0x37B24D;
            vbe_fill_rect(board_x + 6 + px[i] * cell, board_y + 6 + py[i] * cell, dot, dot, col);
        }
    }
}
void vbe_compose_scene(window_t* windows, int win_count, int active_win_idx, int start_vis, int desktop_dir, int drag_file_idx, int mx, int my, int ctx_vis, int ctx_x, int ctx_y, const char** ctx_items, int ctx_count, int ctx_sel) {
    uint32_t bpp_bytes = mode_info->bpp / 8;
    uint32_t size = mode_info->width * mode_info->height * bpp_bytes;
    vbe_memcpy_fast(composition_buffer, wallpaper_buffer, size);
    
    uint8_t* old_target = current_target;
    uint32_t old_width = current_target_width;
    vbe_set_target(composition_buffer, mode_info->width);
    vbe_draw_desktop_icons(desktop_dir);

    uint8_t* old_back = backbuffer;
    backbuffer = composition_buffer;

    for (int i = 0; i < win_count; i++) {
        if (!windows[i].visible || windows[i].minimized) continue;
        
        int is_focused = (i == active_win_idx);
        
        switch(windows[i].type) {
            case WIN_TYPE_TERMINAL:
                if (vga_window_needs_refresh()) vga_refresh_window();
                vbe_blit_window(&windows[i], window_buffer, is_focused);
                break;
            case WIN_TYPE_EXPLORER:
                vbe_blit_window(&windows[i], NULL, is_focused);
                {
                    int cx, cy, cw, ch;
                    vbe_get_window_client_rect(&windows[i], &cx, &cy, &cw, &ch);
                    vbe_draw_breadcrumb(cx, cy, cw, user_explorer_state.current_dir);
                    vbe_draw_explorer_content(cx, cy + 36, cw, ch - 36, user_explorer_state.current_dir);
                }
                break;
            case WIN_TYPE_NARCPAD:
                vbe_blit_window(&windows[i], NULL, is_focused);
                {
                    int cx, cy, cw, ch;
                    vbe_get_window_client_rect(&windows[i], &cx, &cy, &cw, &ch);
                    vbe_draw_narcpad(cx, cy, cw, ch, windows[i].title, user_narcpad_state.content);
                }
                break;
            case WIN_TYPE_SNAKE:
                vbe_blit_window(&windows[i], NULL, is_focused);
                {
                    int cx, cy, cw, ch;
                    vbe_get_window_client_rect(&windows[i], &cx, &cy, &cw, &ch);
                if (user_snake_running()) {
                    vbe_draw_snake_game(cx, cy, cw, ch,
                                        user_snake_state.px, user_snake_state.py,
                                        user_snake_state.len, user_snake_state.apple_x,
                                        user_snake_state.apple_y, user_snake_state.dead,
                                        user_snake_state.score, user_snake_state.best);
                } else {
                    extern int snk_px[100], snk_py[100], snk_len, apple_x, apple_y, snk_dead, snk_score, snk_best;
                    vbe_draw_snake_game(cx, cy, cw, ch, snk_px, snk_py, snk_len, apple_x, apple_y, snk_dead, snk_score, snk_best);
                }
                }
                break;
            case WIN_TYPE_SETTINGS:
                vbe_blit_window(&windows[i], NULL, is_focused);
                {
                    int cx, cy, cw, ch;
                    char time_str[9];
                    char date_str[11];
                    char tz_str[16];
                    int offset = rtc_get_timezone_offset_minutes();
                    int compact;
                    vbe_get_window_client_rect(&windows[i], &cx, &cy, &cw, &ch);
                    rtc_format_timezone(tz_str, sizeof(tz_str));
                    compact = ui_settings_compact_layout(cw);

                    time_str[0] = (char)('0' + (get_hour() / 10));
                    time_str[1] = (char)('0' + (get_hour() % 10));
                    time_str[2] = ':';
                    time_str[3] = (char)('0' + (get_minute() / 10));
                    time_str[4] = (char)('0' + (get_minute() % 10));
                    time_str[5] = ':';
                    time_str[6] = (char)('0' + (get_second() / 10));
                    time_str[7] = (char)('0' + (get_second() % 10));
                    time_str[8] = '\0';

                    date_str[0] = '2';
                    date_str[1] = '0';
                    date_str[2] = (char)('0' + ((get_year() / 10) % 10));
                    date_str[3] = (char)('0' + (get_year() % 10));
                    date_str[4] = '-';
                    date_str[5] = (char)('0' + (get_month() / 10));
                    date_str[6] = (char)('0' + (get_month() % 10));
                    date_str[7] = '-';
                    date_str[8] = (char)('0' + (get_day() / 10));
                    date_str[9] = (char)('0' + (get_day() % 10));
                    date_str[10] = '\0';

                    vbe_fill_rect_alpha(cx, cy, cw, ch, UI_SURFACE_1, 255);
                    vbe_draw_string(cx + 16, cy + 16, "Time", UI_TEXT);
                    vbe_draw_string(cx + 16, cy + 34, "System settings", UI_TEXT_SUBTLE);
                    vbe_fill_rect_alpha(cx + 16, cy + 48, cw - 32, 1, UI_BORDER_SOFT, 255);

                    vbe_draw_string(cx + 20, cy + 66, "Local Time", UI_TEXT_SUBTLE);
                    vbe_draw_string_hd(cx + 20, cy + 86, time_str, UI_TEXT, 2);
                    if (!compact) {
                        vbe_draw_string(cx + cw - 160, cy + 70, "Date", UI_TEXT_SUBTLE);
                        vbe_draw_string(cx + cw - 160, cy + 88, date_str, UI_TEXT);
                        vbe_draw_string(cx + cw - 160, cy + 106, tz_str, UI_ACCENT_ALT);
                    } else {
                        vbe_draw_string(cx + 20, cy + 118, date_str, UI_TEXT);
                        vbe_draw_string(cx + 108, cy + 118, tz_str, UI_ACCENT_ALT);
                    }

                    vbe_fill_rect_alpha(cx + 16, cy + 130, cw - 32, 1, UI_BORDER_SOFT, 255);
                    vbe_draw_string(cx + 20, cy + 148, "Timezone", UI_TEXT);
                    ui_draw_chip(cx + 20, cy + 170, 92, 22, UI_ACCENT_DEEP, UI_TEXT, tz_str);
                    if (!compact) {
                        ui_draw_chip(cx + 124, cy + 170, 56, 22, UI_SURFACE_2, UI_TEXT, "-30m");
                        ui_draw_chip(cx + 188, cy + 170, 56, 22, UI_SURFACE_2, UI_TEXT, "+30m");
                    } else {
                        ui_draw_chip(cx + 20, cy + 198, 56, 22, UI_SURFACE_2, UI_TEXT, "-30m");
                        ui_draw_chip(cx + 84, cy + 198, 56, 22, UI_SURFACE_2, UI_TEXT, "+30m");
                    }

                    vbe_draw_string(cx + 20, cy + (compact ? 226 : 206), "Presets", UI_TEXT_SUBTLE);
                    ui_draw_chip(cx + 20, cy + (compact ? 246 : 226), 64, 22, offset == -300 ? UI_ACCENT_DEEP : UI_SURFACE_2, UI_TEXT, "UTC-5");
                    ui_draw_chip(cx + 92, cy + (compact ? 246 : 226), 44, 22, offset == 0 ? UI_ACCENT_DEEP : UI_SURFACE_2, UI_TEXT, "UTC");
                    ui_draw_chip(cx + 144, cy + (compact ? 246 : 226), 64, 22, offset == 180 ? UI_ACCENT_DEEP : UI_SURFACE_2, UI_TEXT, "UTC+3");
                    if (!compact) {
                        ui_draw_chip(cx + 216, cy + 226, 92, 22, offset == 330 ? UI_ACCENT_DEEP : UI_SURFACE_2, UI_TEXT, "UTC+5:30");
                        ui_draw_chip(cx + 316, cy + 226, 64, 22, offset == 540 ? UI_ACCENT_DEEP : UI_SURFACE_2, UI_TEXT, "UTC+9");
                    } else {
                        ui_draw_chip(cx + 20, cy + 274, 92, 22, offset == 330 ? UI_ACCENT_DEEP : UI_SURFACE_2, UI_TEXT, "UTC+5:30");
                        ui_draw_chip(cx + 120, cy + 274, 64, 22, offset == 540 ? UI_ACCENT_DEEP : UI_SURFACE_2, UI_TEXT, "UTC+9");
                    }

                    vbe_draw_string(cx + 20, cy + (compact ? 306 : 262), "Offset", UI_TEXT_SUBTLE);
                    vbe_draw_int(cx + 72, cy + (compact ? 306 : 262), offset, UI_ACCENT_ALT);
                    vbe_draw_string(cx + 112, cy + (compact ? 306 : 262), "min", UI_TEXT_MUTED);
                }
                break;
        }
    }
    
    if (drag_file_idx != -1) {
        vbe_draw_icon(mx - 16, my - 16, 1, "Moving...", 1);
    }

    vbe_draw_taskbar(start_vis);
    if (start_vis) vbe_draw_start_menu();
    if (ctx_vis) vbe_draw_context_menu(ctx_x, ctx_y, ctx_items, ctx_count, ctx_sel);
    ui_draw_modal();
    vbe_draw_clock();
    
    backbuffer = old_back;
    vbe_set_target(old_target, old_width);
}

void vbe_prepare_frame_from_composition() {
    uint32_t bpp_bytes = mode_info->bpp / 8;
    uint32_t size = mode_info->width * mode_info->height * bpp_bytes;
    vbe_memcpy_fast(backbuffer, composition_buffer, size);
}

void vbe_present_cursor_fast(int old_x, int old_y, int new_x, int new_y) {
    int min_x = old_x < new_x ? old_x : new_x;
    int min_y = old_y < new_y ? old_y : new_y;
    int max_x = old_x > new_x ? old_x : new_x;
    int max_y = old_y > new_y ? old_y : new_y;
    int rect_x = min_x - 1;
    int rect_y = min_y - 1;
    int rect_w = (max_x - min_x) + 14;
    int rect_h = (max_y - min_y) + 14;

    if (rect_x < 0) {
        rect_w += rect_x;
        rect_x = 0;
    }
    if (rect_y < 0) {
        rect_h += rect_y;
        rect_y = 0;
    }
    if (rect_x + rect_w > (int)mode_info->width) rect_w = mode_info->width - rect_x;
    if (rect_y + rect_h > (int)mode_info->height) rect_h = mode_info->height - rect_y;
    if (rect_w <= 0 || rect_h <= 0) return;

    {
        uint32_t bpp = mode_info->bpp / 8;
        for (int row = 0; row < rect_h; row++) {
            uint8_t* dest = backbuffer + ((rect_y + row) * mode_info->width + rect_x) * bpp;
            uint8_t* src = composition_buffer + ((rect_y + row) * mode_info->width + rect_x) * bpp;
            vbe_memcpy_fast(dest, src, rect_w * bpp);
        }
    }

    {
        uint8_t* old_target = current_target;
        uint32_t old_width = current_target_width;
        vbe_set_target(backbuffer, mode_info->width);
        vbe_draw_cursor(new_x, new_y);
        vbe_set_target(old_target, old_width);
    }

    vbe_blit_rect(rect_x, rect_y, rect_w, rect_h, backbuffer, mode_info->width);
}

void vbe_draw_context_menu(int x, int y, const char** items, int count, int selected_idx) {
    int w = 154;
    int h = count * 22 + 8;
    ui_draw_panel(x, y, w, h, UI_RADIUS_MD, UI_SURFACE_1, 246, UI_BORDER_SOFT, 255);
    
    for (int i = 0; i < count; i++) {
        if (i == selected_idx) {
            vbe_draw_rounded_rect(x + 4, y + 4 + i * 22, w - 8, 20, UI_RADIUS_SM, UI_ACCENT_DEEP, 255);
            vbe_draw_string(x + 12, y + 6 + i * 22, items[i], UI_TEXT);
        } else {
            vbe_draw_string(x + 12, y + 6 + i * 22, items[i], UI_TEXT_MUTED);
        }
    }
}
