#ifndef USER_GUI_LIB_H
#define USER_GUI_LIB_H

#include <stddef.h>
#include <stdint.h>

#define MAPLE_MONO_8X8_DECLARE_ONLY
#define MAPLE_MONO_8X8_SYMBOL user_gui_font
#include "maple_mono_8x8.h"
#undef MAPLE_MONO_8X8_SYMBOL
#undef MAPLE_MONO_8X8_DECLARE_ONLY
#include "ui_theme.h"

#ifndef USER_CODE
#define USER_CODE __attribute__((section(".user_code")))
#endif

typedef struct {
    uint32_t* pixels;
    int width;
    int height;
} user_gui_surface_t;

extern const unsigned char user_gui_font[256][8];

static USER_CODE uint32_t user_gui_mix_color(uint32_t fg, uint32_t bg, int alpha) {
    uint32_t rb1;
    uint32_t g1;
    uint32_t rb2;
    uint32_t g2;
    uint32_t rb;
    uint32_t g;

    if (alpha >= 255) return fg;
    if (alpha <= 0) return bg;

    rb1 = fg & 0xFF00FFU;
    g1 = fg & 0x00FF00U;
    rb2 = bg & 0xFF00FFU;
    g2 = bg & 0x00FF00U;
    rb = ((rb1 * (uint32_t)alpha + rb2 * (uint32_t)(256 - alpha)) >> 8) & 0xFF00FFU;
    g = ((g1 * (uint32_t)alpha + g2 * (uint32_t)(256 - alpha)) >> 8) & 0x00FF00U;
    return rb | g;
}

static USER_CODE void user_gui_put_pixel(user_gui_surface_t* surface, int x, int y, uint32_t color) {
    if (!surface || !surface->pixels) return;
    if (x < 0 || y < 0 || x >= surface->width || y >= surface->height) return;
    surface->pixels[y * surface->width + x] = color;
}

static USER_CODE uint32_t user_gui_get_pixel(const user_gui_surface_t* surface, int x, int y) {
    if (!surface || !surface->pixels) return 0;
    if (x < 0 || y < 0 || x >= surface->width || y >= surface->height) return 0;
    return surface->pixels[y * surface->width + x];
}

static USER_CODE void user_gui_put_pixel_alpha(user_gui_surface_t* surface, int x, int y, uint32_t color, int alpha) {
    uint32_t bg;

    if (!surface || alpha <= 0) return;
    if (alpha >= 255) {
        user_gui_put_pixel(surface, x, y, color);
        return;
    }

    bg = user_gui_get_pixel(surface, x, y);
    user_gui_put_pixel(surface, x, y, user_gui_mix_color(color, bg, alpha));
}

static USER_CODE void user_gui_fill_rect(user_gui_surface_t* surface, int x, int y, int w, int h, uint32_t color) {
    int x_end;
    int y_end;

    if (!surface || !surface->pixels || w <= 0 || h <= 0) return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > surface->width) w = surface->width - x;
    if (y + h > surface->height) h = surface->height - y;
    if (w <= 0 || h <= 0) return;

    x_end = x + w;
    y_end = y + h;
    for (int yy = y; yy < y_end; yy++) {
        uint32_t* row = surface->pixels + (size_t)yy * (size_t)surface->width;

        for (int xx = x; xx < x_end; xx++) {
            row[xx] = color;
        }
    }
}

static USER_CODE void user_gui_fill_rect_alpha(user_gui_surface_t* surface, int x, int y, int w, int h, uint32_t color, int alpha) {
    int x_end;
    int y_end;

    if (!surface || alpha <= 0) return;
    if (alpha >= 255) {
        user_gui_fill_rect(surface, x, y, w, h, color);
        return;
    }
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > surface->width) w = surface->width - x;
    if (y + h > surface->height) h = surface->height - y;
    if (w <= 0 || h <= 0) return;

    x_end = x + w;
    y_end = y + h;
    for (int yy = y; yy < y_end; yy++) {
        uint32_t* row = surface->pixels + (size_t)yy * (size_t)surface->width;

        for (int xx = x; xx < x_end; xx++) {
            row[xx] = user_gui_mix_color(color, row[xx], alpha);
        }
    }
}

static USER_CODE int user_gui_round_alpha(int dx, int dy, int r, int base_alpha) {
    int r2;
    int inner2;
    int d2;
    int band;
    int dist_pct;

    if (r <= 1) return base_alpha;
    r2 = r * r;
    inner2 = (r - 1) * (r - 1);
    d2 = dx * dx + dy * dy;
    if (d2 <= inner2) return base_alpha;
    if (d2 > r2) return 0;
    band = r2 - inner2;
    if (band <= 0) return base_alpha;
    dist_pct = (r2 - d2) * 255 / band;
    return (base_alpha * dist_pct) >> 8;
}

static USER_CODE void user_gui_draw_rounded_rect(user_gui_surface_t* surface, int x, int y, int w, int h,
                                                 int r, uint32_t color, int alpha) {
    if (!surface || w <= 0 || h <= 0) return;
    if (r <= 0) {
        user_gui_fill_rect_alpha(surface, x, y, w, h, color, alpha);
        return;
    }

    user_gui_fill_rect_alpha(surface, x + r, y, w - 2 * r, h, color, alpha);
    user_gui_fill_rect_alpha(surface, x, y + r, r, h - 2 * r, color, alpha);
    user_gui_fill_rect_alpha(surface, x + w - r, y + r, r, h - 2 * r, color, alpha);

    for (int yy = 0; yy < r; yy++) {
        for (int xx = 0; xx < r; xx++) {
            user_gui_put_pixel_alpha(surface, x + xx, y + yy, color,
                                     user_gui_round_alpha(r - xx, r - yy, r, alpha));
            user_gui_put_pixel_alpha(surface, x + w - r + xx, y + yy, color,
                                     user_gui_round_alpha(xx + 1, r - yy, r, alpha));
            user_gui_put_pixel_alpha(surface, x + xx, y + h - r + yy, color,
                                     user_gui_round_alpha(r - xx, yy + 1, r, alpha));
            user_gui_put_pixel_alpha(surface, x + w - r + xx, y + h - r + yy, color,
                                     user_gui_round_alpha(xx + 1, yy + 1, r, alpha));
        }
    }
}

static USER_CODE void user_gui_draw_rect(user_gui_surface_t* surface, int x, int y, int w, int h, uint32_t color) {
    user_gui_fill_rect(surface, x, y, w, 1, color);
    user_gui_fill_rect(surface, x, y + h - 1, w, 1, color);
    user_gui_fill_rect(surface, x, y, 1, h, color);
    user_gui_fill_rect(surface, x + w - 1, y, 1, h, color);
}

static USER_CODE int user_gui_glyph_bit(const unsigned char* glyph, int row, int col) {
    if (!glyph || row < 0 || row >= 8 || col < 0 || col >= 8) return 0;
    return (glyph[row] & (1U << (7 - col))) != 0U;
}

static USER_CODE int user_gui_glyph_alpha(const unsigned char* glyph, int row, int col) {
    int direct = 0;
    int diagonal = 0;

    if (user_gui_glyph_bit(glyph, row, col)) return 255;
    for (int oy = -1; oy <= 1; oy++) {
        for (int ox = -1; ox <= 1; ox++) {
            if (ox == 0 && oy == 0) continue;
            if (!user_gui_glyph_bit(glyph, row + oy, col + ox)) continue;
            if (ox == 0 || oy == 0) direct++;
            else diagonal++;
        }
    }
    if (direct == 0 && diagonal == 0) return 0;
    return direct * 44 + diagonal * 20;
}

static USER_CODE void user_gui_draw_char(user_gui_surface_t* surface, int x, int y, char c, uint32_t color) {
    const unsigned char* glyph;
    int row;
    int col;

    if (!surface) return;
    glyph = user_gui_font[(uint8_t)c];
    for (row = -1; row <= 8; row++) {
        for (col = -1; col <= 8; col++) {
            int alpha = user_gui_glyph_alpha(glyph, row, col);
            if (alpha > 0) user_gui_put_pixel_alpha(surface, x + col, y + row, color, alpha);
        }
    }
}

static USER_CODE void user_gui_draw_string(user_gui_surface_t* surface, int x, int y, const char* s, uint32_t color) {
    if (!surface || !s) return;
    while (*s) {
        user_gui_draw_char(surface, x, y, *s, color);
        x += 8;
        s++;
    }
}

static USER_CODE __attribute__((unused)) void user_gui_draw_char_crisp(user_gui_surface_t* surface,
                                                                       int x, int y, char c, uint32_t color) {
    const unsigned char* glyph;

    if (!surface) return;
    glyph = user_gui_font[(uint8_t)c];
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (user_gui_glyph_bit(glyph, row, col)) user_gui_put_pixel(surface, x + col, y + row, color);
        }
    }
}

static USER_CODE __attribute__((unused)) void user_gui_draw_string_crisp(user_gui_surface_t* surface,
                                                                         int x, int y, const char* s,
                                                                         uint32_t color) {
    if (!surface || !s) return;
    while (*s) {
        user_gui_draw_char_crisp(surface, x, y, *s, color);
        x += 8;
        s++;
    }
}

static USER_CODE __attribute__((unused)) void user_gui_draw_char_crisp_tall(user_gui_surface_t* surface,
                                                                            int x, int y, char c, uint32_t color) {
    const unsigned char* glyph;

    if (!surface) return;
    glyph = user_gui_font[(uint8_t)c];
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (!user_gui_glyph_bit(glyph, row, col)) continue;
            user_gui_put_pixel(surface, x + col, y + row * 2, color);
            user_gui_put_pixel(surface, x + col, y + row * 2 + 1, color);
        }
    }
}

static USER_CODE __attribute__((unused)) void user_gui_draw_string_crisp_tall(user_gui_surface_t* surface,
                                                                              int x, int y, const char* s,
                                                                              uint32_t color) {
    if (!surface || !s) return;
    while (*s) {
        user_gui_draw_char_crisp_tall(surface, x, y, *s, color);
        x += 8;
        s++;
    }
}

static USER_CODE __attribute__((unused)) void user_gui_draw_int_crisp_tall(user_gui_surface_t* surface,
                                                                           int x, int y, int value, uint32_t color) {
    char buf[16];
    int pos = 0;
    unsigned int n;

    if (!surface) return;
    if (value == 0) {
        user_gui_draw_char_crisp_tall(surface, x, y, '0', color);
        return;
    }
    if (value < 0) {
        user_gui_draw_char_crisp_tall(surface, x, y, '-', color);
        x += 8;
        n = (unsigned int)(-value);
    } else {
        n = (unsigned int)value;
    }
    while (n != 0U && pos < (int)(sizeof(buf) - 1U)) {
        buf[pos++] = (char)('0' + (n % 10U));
        n /= 10U;
    }
    while (pos > 0) {
        user_gui_draw_char_crisp_tall(surface, x, y, buf[--pos], color);
        x += 8;
    }
}

static USER_CODE __attribute__((unused)) void user_gui_draw_int_crisp(user_gui_surface_t* surface,
                                                                      int x, int y, int value, uint32_t color) {
    char buf[16];
    int pos = 0;
    unsigned int n;

    if (!surface) return;
    if (value == 0) {
        user_gui_draw_char_crisp(surface, x, y, '0', color);
        return;
    }
    if (value < 0) {
        user_gui_draw_char_crisp(surface, x, y, '-', color);
        x += 8;
        n = (unsigned int)(-value);
    } else {
        n = (unsigned int)value;
    }
    while (n != 0U && pos < (int)(sizeof(buf) - 1U)) {
        buf[pos++] = (char)('0' + (n % 10U));
        n /= 10U;
    }
    while (pos > 0) {
        user_gui_draw_char_crisp(surface, x, y, buf[--pos], color);
        x += 8;
    }
}

static USER_CODE __attribute__((unused)) int user_gui_text_width(const char* s, int advance) {
    int len = 0;

    if (!s) return 0;
    if (advance <= 0) advance = 8;
    while (s[len] != '\0') len++;
    return len * advance;
}

static USER_CODE __attribute__((unused)) int user_gui_tall_glyph_alpha(const unsigned char* glyph, int row, int col) {
    int src_row;
    int a0;
    int a1;

    if (col < -1 || col > 8 || row < -1 || row > 16) return 0;
    if (row < 0) src_row = -1;
    else if (row >= 16) src_row = 8;
    else src_row = row >> 1;
    a0 = user_gui_glyph_alpha(glyph, src_row, col);
    if ((row & 1) == 0 || row < 0 || row >= 15) return a0;
    a1 = user_gui_glyph_alpha(glyph, src_row + 1, col);
    return (a0 * 3 + a1) >> 2;
}

static USER_CODE __attribute__((unused)) void user_gui_draw_char_tall(user_gui_surface_t* surface,
                                                                      int x, int y, char c, uint32_t color) {
    const unsigned char* glyph;

    if (!surface) return;
    glyph = user_gui_font[(uint8_t)c];
    for (int row = -1; row <= 16; row++) {
        for (int col = -1; col <= 8; col++) {
            int alpha = user_gui_tall_glyph_alpha(glyph, row, col);

            if (alpha > 0) user_gui_put_pixel_alpha(surface, x + col, y + row, color, alpha);
        }
    }
}

static USER_CODE __attribute__((unused)) void user_gui_draw_string_tall(user_gui_surface_t* surface,
                                                                       int x, int y, const char* s, uint32_t color) {
    if (!surface || !s) return;
    while (*s) {
        user_gui_draw_char_tall(surface, x, y, *s, color);
        x += 9;
        s++;
    }
}

static USER_CODE __attribute__((unused)) void user_gui_draw_string_tall_shadow(user_gui_surface_t* surface,
                                                                              int x, int y, const char* s,
                                                                              uint32_t color, uint32_t shadow) {
    if (!surface || !s) return;
    user_gui_draw_string_tall(surface, x + 1, y + 1, s, shadow);
    user_gui_draw_string_tall(surface, x, y, s, color);
}

enum {
    USER_GUI_ICON_NARCOS = 1,
    USER_GUI_ICON_TERMINAL = 2,
    USER_GUI_ICON_SETTINGS = 3,
    USER_GUI_ICON_EXPLORER = 4,
    USER_GUI_ICON_FOLDER = 5,
    USER_GUI_ICON_FILE = 6,
    USER_GUI_ICON_SNAKE = 7,
    USER_GUI_ICON_NARCPAD = 8,
    USER_GUI_ICON_APP = 9,
    USER_GUI_ICON_INFO = 10,
    USER_GUI_ICON_DOOM = 11
};

static USER_CODE __attribute__((unused)) int user_gui_icon_scale(int value, int size) {
    return (value * size + 16) / 32;
}

static USER_CODE __attribute__((unused)) void user_gui_icon_rect(user_gui_surface_t* surface, int x, int y, int size,
                                                                int gx, int gy, int gw, int gh, int radius,
                                                                uint32_t color, int alpha) {
    int sx;
    int sy;
    int sw;
    int sh;
    int sr;

    if (!surface || gw <= 0 || gh <= 0 || size <= 0) return;
    sx = x + user_gui_icon_scale(gx, size);
    sy = y + user_gui_icon_scale(gy, size);
    sw = user_gui_icon_scale(gw, size);
    sh = user_gui_icon_scale(gh, size);
    sr = user_gui_icon_scale(radius, size);
    if (sw < 1) sw = 1;
    if (sh < 1) sh = 1;
    if (sr < 1 && radius > 0) sr = 1;
    user_gui_draw_rounded_rect(surface, sx, sy, sw, sh, sr, color, alpha);
}

static USER_CODE __attribute__((unused)) void user_gui_icon_fill(user_gui_surface_t* surface, int x, int y, int size,
                                                                int gx, int gy, int gw, int gh, uint32_t color) {
    int sx;
    int sy;
    int sw;
    int sh;

    if (!surface || gw <= 0 || gh <= 0 || size <= 0) return;
    sx = x + user_gui_icon_scale(gx, size);
    sy = y + user_gui_icon_scale(gy, size);
    sw = user_gui_icon_scale(gw, size);
    sh = user_gui_icon_scale(gh, size);
    if (sw < 1) sw = 1;
    if (sh < 1) sh = 1;
    user_gui_fill_rect(surface, sx, sy, sw, sh, color);
}

static USER_CODE __attribute__((unused)) void user_gui_draw_icon(user_gui_surface_t* surface, int icon,
                                                                int x, int y, int size,
                                                                uint32_t accent, int selected) {
    uint32_t light;
    uint32_t pale;
    uint32_t deep;
    uint32_t glass;
    uint32_t edge;

    if (!surface || size <= 0) return;
    if (size < 12) size = 12;
    light = user_gui_mix_color(0xFFFFFF, accent, selected ? 126 : 86);
    pale = user_gui_mix_color(0xEEF4FA, accent, selected ? 150 : 110);
    deep = user_gui_mix_color(0x081019, accent, selected ? 48 : 30);
    glass = user_gui_mix_color(accent, UI_SURFACE_0, selected ? 138 : 78);
    edge = selected ? light : user_gui_mix_color(UI_BORDER_SOFT, accent, 96);

    switch (icon) {
        case USER_GUI_ICON_NARCOS:
            user_gui_icon_rect(surface, x, y, size, 5, 12, 5, 15, 3, deep, 255);
            user_gui_icon_rect(surface, x, y, size, 9, 7, 14, 5, 3, accent, 255);
            user_gui_icon_rect(surface, x, y, size, 21, 11, 5, 16, 3, accent, 255);
            user_gui_icon_fill(surface, x, y, size, 10, 8, 10, 1, light);
            break;
        case USER_GUI_ICON_TERMINAL:
            user_gui_icon_rect(surface, x, y, size, 3, 6, 26, 20, 4, UI_SURFACE_0, 245);
            user_gui_icon_rect(surface, x, y, size, 3, 6, 26, 20, 4, edge, 130);
            user_gui_icon_fill(surface, x, y, size, 8, 12, 3, 2, accent);
            user_gui_icon_fill(surface, x, y, size, 11, 14, 3, 2, accent);
            user_gui_icon_fill(surface, x, y, size, 8, 16, 3, 2, accent);
            user_gui_icon_fill(surface, x, y, size, 16, 19, 9, 2, pale);
            break;
        case USER_GUI_ICON_SETTINGS:
            user_gui_icon_fill(surface, x, y, size, 14, 3, 4, 5, light);
            user_gui_icon_fill(surface, x, y, size, 14, 24, 4, 5, light);
            user_gui_icon_fill(surface, x, y, size, 3, 14, 5, 4, light);
            user_gui_icon_fill(surface, x, y, size, 24, 14, 5, 4, light);
            user_gui_icon_fill(surface, x, y, size, 7, 7, 4, 4, light);
            user_gui_icon_fill(surface, x, y, size, 21, 7, 4, 4, light);
            user_gui_icon_fill(surface, x, y, size, 7, 21, 4, 4, light);
            user_gui_icon_fill(surface, x, y, size, 21, 21, 4, 4, light);
            user_gui_icon_rect(surface, x, y, size, 8, 8, 16, 16, 8, accent, 255);
            user_gui_icon_rect(surface, x, y, size, 12, 12, 8, 8, 4, pale, 255);
            user_gui_icon_rect(surface, x, y, size, 14, 14, 4, 4, 2, deep, 255);
            break;
        case USER_GUI_ICON_EXPLORER:
        case USER_GUI_ICON_FOLDER:
            user_gui_icon_rect(surface, x, y, size, 3, 11, 26, 16, 4, accent, 255);
            user_gui_icon_rect(surface, x, y, size, 6, 7, 12, 6, 3, light, 255);
            user_gui_icon_fill(surface, x, y, size, 7, 14, 19, 2, 0xFFF6D8);
            user_gui_icon_fill(surface, x, y, size, 8, 21, 16, 1, pale);
            break;
        case USER_GUI_ICON_FILE:
            user_gui_icon_rect(surface, x, y, size, 8, 3, 17, 26, 3, pale, 255);
            user_gui_icon_fill(surface, x, y, size, 20, 3, 5, 6, light);
            user_gui_icon_fill(surface, x, y, size, 11, 12, 11, 2, accent);
            user_gui_icon_fill(surface, x, y, size, 11, 17, 11, 2, glass);
            user_gui_icon_fill(surface, x, y, size, 11, 22, 8, 2, glass);
            break;
        case USER_GUI_ICON_SNAKE:
            user_gui_icon_rect(surface, x, y, size, 5, 5, 22, 22, 7, accent, 255);
            user_gui_icon_fill(surface, x, y, size, 9, 9, 5, 5, deep);
            user_gui_icon_fill(surface, x, y, size, 14, 9, 5, 5, deep);
            user_gui_icon_fill(surface, x, y, size, 19, 9, 5, 5, deep);
            user_gui_icon_fill(surface, x, y, size, 19, 14, 5, 5, deep);
            user_gui_icon_fill(surface, x, y, size, 14, 14, 5, 5, deep);
            user_gui_icon_fill(surface, x, y, size, 9, 20, 5, 5, UI_DANGER);
            break;
        case USER_GUI_ICON_DOOM:
            user_gui_icon_rect(surface, x, y, size, 4, 7, 24, 18, 3, deep, 255);
            user_gui_icon_rect(surface, x, y, size, 6, 9, 20, 14, 2, accent, 255);
            user_gui_icon_fill(surface, x, y, size, 8, 12, 16, 2, pale);
            user_gui_icon_fill(surface, x, y, size, 8, 16, 5, 5, 0x18110C);
            user_gui_icon_fill(surface, x, y, size, 19, 16, 5, 5, 0x18110C);
            user_gui_icon_fill(surface, x, y, size, 14, 19, 4, 3, UI_DANGER);
            user_gui_icon_fill(surface, x, y, size, 11, 25, 10, 2, light);
            break;
        case USER_GUI_ICON_NARCPAD:
            user_gui_icon_rect(surface, x, y, size, 7, 4, 18, 24, 3, pale, 255);
            user_gui_icon_fill(surface, x, y, size, 20, 4, 5, 6, light);
            user_gui_icon_fill(surface, x, y, size, 11, 11, 2, 11, accent);
            user_gui_icon_fill(surface, x, y, size, 13, 13, 2, 2, accent);
            user_gui_icon_fill(surface, x, y, size, 15, 15, 2, 2, accent);
            user_gui_icon_fill(surface, x, y, size, 17, 11, 2, 11, accent);
            break;
        case USER_GUI_ICON_INFO:
            user_gui_icon_rect(surface, x, y, size, 7, 7, 18, 18, 9, accent, 255);
            user_gui_icon_fill(surface, x, y, size, 15, 14, 3, 8, deep);
            user_gui_icon_fill(surface, x, y, size, 15, 10, 3, 3, pale);
            break;
        case USER_GUI_ICON_APP:
        default:
            user_gui_icon_rect(surface, x, y, size, 5, 6, 22, 20, 4, glass, 255);
            user_gui_icon_rect(surface, x, y, size, 5, 6, 22, 20, 4, edge, 160);
            user_gui_icon_fill(surface, x, y, size, 8, 10, 16, 2, accent);
            user_gui_icon_fill(surface, x, y, size, 8, 15, 11, 2, pale);
            user_gui_icon_fill(surface, x, y, size, 8, 20, 13, 2, pale);
            break;
    }
}

static USER_CODE void user_gui_draw_int(user_gui_surface_t* surface, int x, int y, int value, uint32_t color) {
    char buf[16];
    int pos = 0;
    unsigned int n;

    if (!surface) return;
    if (value == 0) {
        user_gui_draw_char(surface, x, y, '0', color);
        return;
    }
    if (value < 0) {
        user_gui_draw_char(surface, x, y, '-', color);
        x += 8;
        n = (unsigned int)(-value);
    } else {
        n = (unsigned int)value;
    }
    while (n != 0U && pos < (int)(sizeof(buf) - 1U)) {
        buf[pos++] = (char)('0' + (n % 10U));
        n /= 10U;
    }
    while (pos > 0) {
        user_gui_draw_char(surface, x, y, buf[--pos], color);
        x += 8;
    }
}

static USER_CODE void user_gui_draw_chip(user_gui_surface_t* surface, int x, int y, int w, int h,
                                         uint32_t fill, uint32_t text, const char* label) {
    int label_len = 0;
    int tx;
    int ty;

    if (!surface || !label) return;
    while (label[label_len] != '\0') label_len++;

    user_gui_draw_rounded_rect(surface, x, y, w, h, UI_RADIUS_SM, fill, 255);
    user_gui_draw_rect(surface, x, y, w, h, UI_BORDER_SOFT);
    tx = x + ((w - (label_len * 8)) / 2);
    ty = y + ((h - 8) / 2);
    if (tx < x + 6) tx = x + 6;
    user_gui_draw_string(surface, tx, ty, label, text);
}

static USER_CODE void user_gui_draw_panel(user_gui_surface_t* surface, int x, int y, int w, int h,
                                          uint32_t fill, uint32_t border) {
    user_gui_draw_rounded_rect(surface, x, y, w, h, UI_RADIUS_MD, fill, 255);
    user_gui_draw_rect(surface, x, y, w, h, border);
}

#endif
