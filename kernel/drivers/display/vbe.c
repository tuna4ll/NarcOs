#include <stdint.h>
#include "vbe.h"
#include "cpu.h"
#include "string.h"
#include "fs.h"
#include "rtc.h"
#include "usermode.h"
#include "ui_theme.h"
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

static void vbe_memcpy_fast(void* dest, const void* src, uint32_t count) {
    if (cpu_sse_enabled()) vbe_memcpy_sse(dest, (void*)src, count);
    else vbe_memcpy(dest, (void*)src, count);
}

static void vbe_memset_fast(void* dest, uint32_t color, uint32_t count_bytes) {
    if (cpu_sse_enabled()) {
        vbe_memset_sse(dest, color, count_bytes);
    } else {
        uint32_t* pixels = (uint32_t*)dest;
        uint32_t count = count_bytes / 4U;
        for (uint32_t i = 0; i < count; i++) {
            pixels[i] = color;
        }
    }
}

static void vbe_alpha_blend_fast(void* dest, uint32_t color, uint32_t alpha, uint32_t count_pixels) {
    if (cpu_sse_enabled()) {
        vbe_alpha_blend_sse(dest, color, alpha, count_pixels);
    } else {
        uint32_t* pixels = (uint32_t*)dest;
        for (uint32_t i = 0; i < count_pixels; i++) {
            pixels[i] = vbe_mix_color(color, pixels[i], (int)alpha);
        }
    }
}

static void vbe_draw_glyph_solid_32(int x, int y, const unsigned char* glyph, uint32_t color) {
    int start_col = 0;
    int end_col = 8;
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
    ['!'] = {0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x00},
    ['"'] = {0x36, 0x36, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['#'] = {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00},
    ['$'] = {0x18, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x18, 0x00},
    ['%'] = {0x62, 0x64, 0x08, 0x10, 0x26, 0x46, 0x00, 0x00},
    ['&'] = {0x30, 0x48, 0x30, 0x4A, 0x44, 0x3A, 0x00, 0x00},
    ['\''] = {0x18, 0x18, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['('] = {0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00},
    [')'] = {0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00},
    ['*'] = {0x00, 0x66, 0x3C, 0x7E, 0x3C, 0x66, 0x00, 0x00},
    ['+'] = {0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00},
    [','] = {0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x10, 0x20},
    ['-'] = {0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00},
    [':'] = {0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00},
    [';'] = {0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x10, 0x20},
    ['<'] = {0x0C, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0C, 0x00},
    ['='] = {0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00},
    ['>'] = {0x30, 0x18, 0x0C, 0x06, 0x0C, 0x18, 0x30, 0x00},
    ['?'] = {0x3C, 0x66, 0x06, 0x0C, 0x18, 0x00, 0x18, 0x00},
    ['@'] = {0x3C, 0x42, 0x5A, 0x5E, 0x5C, 0x40, 0x3C, 0x00},
    ['['] = {0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00},
    ['\\'] = {0x00, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x00, 0x00},
    [']'] = {0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00},
    ['^'] = {0x18, 0x3C, 0x66, 0x42, 0x00, 0x00, 0x00, 0x00},
    ['_'] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00},
    ['`'] = {0x18, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['{'] = {0x0E, 0x18, 0x18, 0x70, 0x18, 0x18, 0x0E, 0x00},
    ['|'] = {0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00},
    ['}'] = {0x70, 0x18, 0x18, 0x0E, 0x18, 0x18, 0x70, 0x00},
    ['~'] = {0x32, 0x4C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['/'] = {0x00, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x00, 0x00},
    ['.'] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00},
};

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

static unsigned char glyph_c_cedilla_upper[8] = {0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x18};
static unsigned char glyph_g_breve_upper[8]   = {0x1C, 0x00, 0x3C, 0x66, 0x60, 0x6E, 0x66, 0x3C};
static unsigned char glyph_i_dotted_upper[8]  = {0x18, 0x00, 0x3C, 0x18, 0x18, 0x18, 0x18, 0x3C};
static unsigned char glyph_o_umlaut_upper[8]  = {0x66, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x66, 0x3C};
static unsigned char glyph_s_cedilla_upper[8] = {0x3C, 0x66, 0x30, 0x18, 0x0C, 0x66, 0x3C, 0x18};
static unsigned char glyph_u_umlaut_upper[8]  = {0x66, 0x00, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C};
static unsigned char glyph_c_cedilla_lower[8] = {0x00, 0x00, 0x3C, 0x60, 0x60, 0x66, 0x3C, 0x18};
static unsigned char glyph_g_breve_lower[8]   = {0x1C, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x3C};
static unsigned char glyph_dotless_i_lower[8] = {0x00, 0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x3C};
static unsigned char glyph_o_umlaut_lower[8]  = {0x66, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x66, 0x3C};
static unsigned char glyph_s_cedilla_lower[8] = {0x00, 0x00, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x18};
static unsigned char glyph_u_umlaut_lower[8]  = {0x66, 0x00, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3E};

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
        vbe_memcpy_fast((void*)mode_info->framebuffer, backbuffer, mode_info->width * mode_info->height * bpp_bytes);
    } else {
        for(uint32_t y = 0; y < mode_info->height; y++) {
            vbe_memcpy_fast((void*)(mode_info->framebuffer + y * mode_info->pitch), backbuffer + y * row_size, row_size);
        }
    }
}

void wait_vsync() {
    while (inb(0x3DA) & 8);
    while (!(inb(0x3DA) & 8));
}

void init_vbe() {
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
        uint8_t* old_target = current_target;
        uint32_t old_width = current_target_width;
        vbe_set_target(wallpaper_buffer, mode_info->width);
        vbe_fill_rect_alpha(0, 0, mode_info->width, 90, 0x04080D, 120);
        vbe_fill_rect_alpha(0, mode_info->height - 120, mode_info->width, 120, 0x03060A, 110);
        vbe_fill_rect_alpha(48, 54, 360, 360, UI_ACCENT_DEEP, 18);
        vbe_fill_rect_alpha(72, 78, 320, 320, UI_ACCENT_ALT, 10);
        vbe_fill_rect_alpha(mode_info->width - 420, 110, 300, 300, UI_ACCENT_DEEP, 12);
        vbe_set_target(old_target, old_width);
        wallpaper_init = 1;
    }
    vbe_memcpy_fast(backbuffer, wallpaper_buffer, screen_size);
}

void vbe_draw_cursor(int x, int y) {
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
            for (int col = 0; col < 8; col++) {
                if (glyph[row] & (1 << (7 - col))) vbe_put_pixel_alpha(x + col, y + row, color, 255);
            }
        }
        return;
    }

    for (int row = 0; row < 8 * scale; row++) {
        for (int col = 0; col < 8 * scale; col++) {
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
            for (int col = 0; col < 8; col++) {
                if (glyph[row] & (1 << (7 - col))) vbe_put_pixel_alpha(x + col, y + row, color, 255);
            }
        }
        return;
    }
    for (int row = 0; row < 8 * scale; row++) {
        for (int col = 0; col < 8 * scale; col++) {
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

static void ui_draw_panel(int x, int y, int w, int h, int radius, uint32_t fill, int fill_alpha, uint32_t border, int border_alpha) {
    vbe_draw_shadow(x + 4, y + 6, w, h, radius);
    vbe_draw_rounded_rect(x, y, w, h, radius, fill, fill_alpha);
    vbe_draw_rounded_rect(x, y, w, h, radius, border, border_alpha);
}

static void ui_draw_panel_flat(int x, int y, int w, int h, int radius, uint32_t fill, int fill_alpha, uint32_t border, int border_alpha) {
    vbe_draw_rounded_rect(x, y, w, h, radius, fill, fill_alpha);
    vbe_draw_rounded_rect(x, y, w, h, radius, border, border_alpha);
}

static void ui_draw_chip(int x, int y, int w, int h, uint32_t fill, uint32_t text, const char* label) {
    vbe_draw_rounded_rect(x, y, w, h, UI_RADIUS_SM, fill, 220);
    if (label) vbe_draw_string(x + 9, y + 6, label, text);
}

static void ui_draw_app_toolbar(int x, int y, int w, const char* app_name, const char* meta) {
    (void)app_name;
    (void)meta;
    vbe_fill_rect_alpha(x, y + 24, w, 1, UI_BORDER_SOFT, 255);
}

static void ui_draw_modal(void) {
    extern int exp_modal_mode;
    extern char exp_modal_input[32];
    extern int exp_selected;
    extern disk_fs_node_t dir_cache[MAX_FILES];
    int sw = (int)mode_info->width;
    int sh = (int)mode_info->height;
    int w = 320;
    int h = 140;
    int x = (sw - w) / 2;
    int y = (sh - h) / 2;
    if (exp_modal_mode == 0) return;
    vbe_fill_rect_alpha(0, 0, sw, sh, 0x02060A, 110);
    ui_draw_panel(x, y, w, h, UI_RADIUS_LG, UI_SURFACE_1, 250, UI_BORDER_STRONG, 255);
    if (exp_modal_mode == 1) {
        vbe_draw_string(x + 20, y + 20, "Rename Item", UI_TEXT);
        vbe_draw_string(x + 20, y + 38, "Type a new name and press Enter.", UI_TEXT_MUTED);
        vbe_fill_rect_alpha(x + 20, y + 60, w - 40, 28, UI_SURFACE_0, 255);
        vbe_draw_rect(x + 20, y + 60, w - 40, 28, UI_BORDER_SOFT);
        vbe_draw_string(x + 28, y + 69, exp_modal_input, UI_TEXT);
        ui_draw_chip(x + w - 136, y + h - 32, 50, 18, UI_SURFACE_2, UI_TEXT_MUTED, "Esc");
        ui_draw_chip(x + w - 78, y + h - 32, 54, 18, UI_ACCENT_DEEP, UI_TEXT, "Enter");
    } else if (exp_modal_mode == 2) {
        vbe_draw_string(x + 20, y + 20, "Delete Item", UI_DANGER);
        vbe_draw_string(x + 20, y + 42, "Delete the selected item?", UI_TEXT);
        if (exp_selected >= 0 && dir_cache[exp_selected].flags != 0) {
            vbe_draw_string(x + 20, y + 60, dir_cache[exp_selected].name, UI_TEXT_MUTED);
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

    vbe_draw_shadow(x + 4, y + 6, w, h, UI_RADIUS_LG);
    vbe_draw_rounded_rect(x, y, w, h, UI_RADIUS_LG, COLOR_GLASS_BG, 228);

    uint32_t title_top = is_focused ? UI_SURFACE_3 : UI_SURFACE_2;
    uint32_t title_bottom = is_focused ? UI_SURFACE_2 : UI_SURFACE_1;
    vbe_fill_rect_gradient(x + 1, y + 1, w - 2, 34, title_top, title_bottom, 1);
    vbe_fill_rect_alpha(x + 1, y + 34, w - 2, 1, is_focused ? UI_ACCENT : UI_BORDER_SOFT, 180);
    vbe_fill_rect_alpha(x + WINDOW_CLIENT_INSET_X, y + WINDOW_CLIENT_TOP,
                        w - WINDOW_CLIENT_INSET_X * 2, h - WINDOW_CLIENT_TOP - WINDOW_CLIENT_BOTTOM,
                        UI_SURFACE_0, 210);

    {
        char title_buf[20];
        int title_chars = (w - 96) / 8;
        if (title_chars < 6) title_chars = 6;
        if (title_chars > 19) title_chars = 19;
        ui_copy_truncated(title_buf, win->title, title_chars);
        vbe_draw_string(x + 16, y + 12, title_buf, COLOR_TEXT);
    }

    vbe_fill_rect_alpha(x + w - 44, y + 10, 10, 10, UI_WARNING, 255);
    vbe_fill_rect_alpha(x + w - 24, y + 10, 10, 10, UI_DANGER, 255);

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
    uint32_t bpp = mode_info->bpp / 8;
    for (int i = 0; i < h; i++) {
        if (y + i < 0 || (uint32_t)(y + i) >= mode_info->height) continue;
        if (x < 0 || (uint32_t)x >= mode_info->width) continue;
        uint32_t draw_w = (x + w > (int)mode_info->width) ? (mode_info->width - x) : w;
        uint8_t* dest = (uint8_t*)mode_info->framebuffer + ((y + i) * mode_info->pitch + x * bpp);
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
    vbe_draw_rounded_rect(x + 2, y + 2, w + 4, h + 4, r + 2, 0x000000, 30);
    vbe_draw_rounded_rect(x + 4, y + 4, w + 8, h + 8, r + 4, 0x000000, 15);
}

void vbe_draw_taskbar(int start_btn_active) {
    uint32_t w = mode_info->width;
    uint32_t tb_h = 35;
    vbe_fill_rect_gradient(0, 0, w, tb_h, UI_TASKBAR_MID, UI_TASKBAR_BG, 1);
    vbe_fill_rect_alpha(0, 0, w, 1, UI_TASKBAR_EDGE, 140);
    vbe_fill_rect_alpha(0, tb_h - 1, w, 1, UI_BORDER_SOFT, 180);
    
    uint32_t btn_color = start_btn_active ? UI_ACCENT_DEEP : UI_SURFACE_2;
    vbe_draw_rounded_rect(5, 4, 84, 27, UI_RADIUS_SM, btn_color, 255);
    vbe_draw_string(20, 12, "Narc", COLOR_TEXT);

    vbe_draw_rounded_rect(96, 4, 40, 27, UI_RADIUS_SM, UI_SURFACE_2, 220);
    vbe_draw_vector_terminal(101, 0);
    
    int app_x = 148;
    int slot_w = 104;
    int slot_gap = 8;
    int max_x = (int)w - 118;
    extern window_t windows[MAX_WINDOWS];
    extern int window_count;
    extern int active_window_idx;
    
    for (int i = 0; i < window_count; i++) {
        if (!windows[i].visible) continue;
        
        char title_buf[13];
        uint32_t app_col;
        if (app_x + slot_w > max_x) break;
        app_col = (i == active_window_idx) ? UI_ACCENT_DEEP : UI_SURFACE_2;
        ui_copy_truncated(title_buf, windows[i].title, 12);
        vbe_draw_rounded_rect(app_x, 4, slot_w, 27, UI_RADIUS_SM, app_col, 220);
        vbe_draw_string(app_x + 10, 12, title_buf, (i == active_window_idx) ? UI_TEXT : UI_TEXT_MUTED);
        app_x += slot_w + slot_gap;
    }
}

void vbe_draw_start_menu() {
    ui_draw_panel(8, 41, 260, 356, UI_RADIUS_LG, UI_SURFACE_1, 244, UI_BORDER_SOFT, 255);
    vbe_fill_rect_gradient(9, 42, 258, 64, UI_SURFACE_3, UI_SURFACE_2, 1);
    vbe_draw_string(24, 58, "NarcOS", UI_TEXT);
    vbe_draw_string(24, 76, "Professional Workstation", UI_TEXT_MUTED);

    ui_draw_chip(20, 122, 220, 24, UI_SURFACE_2, UI_TEXT, "Terminal");
    ui_draw_chip(20, 154, 220, 24, UI_SURFACE_2, UI_TEXT, "Snake");
    ui_draw_chip(20, 186, 220, 24, UI_SURFACE_2, UI_TEXT, "NarcPad");
    ui_draw_chip(20, 218, 220, 24, UI_SURFACE_2, UI_TEXT, "Settings");

    vbe_fill_rect_alpha(20, 286, 220, 1, UI_BORDER_SOFT, 255);
    vbe_draw_string(20, 306, "SESSION", UI_TEXT_SUBTLE);
    vbe_draw_string(20, 326, "narc desktop session", UI_ACCENT_ALT);
    vbe_draw_string(20, 346, "x86 experimental desktop", UI_TEXT_MUTED);
}
void vbe_fill_rect_gradient(int x, int y, int w, int h, uint32_t c1, uint32_t c2, int vertical) {
    (void)vertical;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)current_target_width) w = current_target_width - x;
    if (y + h > (int)mode_info->height) h = mode_info->height - y;
    if (w <= 0 || h <= 0) return;

    if (mode_info->bpp == 32) {
        for (int i = 0; i < h; i++) {
            int ratio = (i * 255) / h;
            uint32_t color = vbe_mix_color(c2, c1, ratio);
            uint32_t* row = (uint32_t*)(current_target + ((y + i) * current_target_width + x) * 4U);
            vbe_memset_fast(row, color, (uint32_t)(w * 4));
        }
        return;
    }

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
    ui_draw_chip(w - 104, 8, 90, 20, UI_SURFACE_2, UI_TEXT, 0);
    vbe_draw_string(w - 91, 14, time_str, UI_ACCENT_ALT);
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
        vbe_draw_rounded_rect(x - 12, y - 10, 60, 70, UI_RADIUS_MD, UI_SURFACE_2, 220);
        vbe_draw_rounded_rect(x - 12, y - 10, 60, 70, UI_RADIUS_MD, UI_ACCENT, 160);
    }
    
    if (type == 0) vbe_draw_vector_folder(x, y, selected);
    else if (type == 2) vbe_draw_vector_pc(x, y);
    else if (type == 3) vbe_draw_vector_snake(x, y);
    else vbe_draw_vector_file(x, y, selected);

    vbe_draw_string(x - 8, y + 44, label, selected ? UI_TEXT : UI_TEXT_MUTED);
}
#include "fs.h"
extern disk_fs_node_t dir_cache[MAX_FILES];

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
    uint32_t fill = selected ? UI_ACCENT_DEEP : ((type == 2) ? UI_SURFACE_2 : UI_SURFACE_1);
    uint32_t border = selected ? UI_ACCENT_ALT : UI_BORDER_SOFT;
    ui_draw_panel_flat(x, y, w, h, UI_RADIUS_MD, fill, 235, border, 255);
    if (selected) vbe_fill_rect_alpha(x + 1, y + 1, 3, h - 2, UI_ACCENT_ALT, 255);
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

void vbe_draw_explorer_content(int x, int y, int w, int h, int current_dir) {
    int sidebar_w = 136;
    int content_x = x + sidebar_w + 14;
    int content_w = w - sidebar_w - 26;
    int panel_y = y;
    int panel_h = h;
    int toolbar_y = panel_y + 8;
    int list_y = panel_y + 48;
    int item_count = ui_count_children(current_dir);
    int content_h = panel_h;
    extern int exp_selected;

    ui_draw_panel_flat(x, panel_y, sidebar_w, panel_h, UI_RADIUS_MD, UI_SURFACE_1, 235, UI_BORDER_SOFT, 255);
    vbe_draw_string(x + 14, panel_y + 16, "PLACES", UI_TEXT_SUBTLE);
    ui_draw_chip(x + 12, panel_y + 34, 110, 22, current_dir == -1 ? UI_ACCENT_DEEP : UI_SURFACE_2, UI_TEXT, "Root");
    ui_draw_chip(x + 12, panel_y + 62, 110, 22, UI_SURFACE_2, UI_TEXT, "Desktop");
    ui_draw_chip(x + 12, panel_y + 90, 110, 22, UI_SURFACE_2, UI_TEXT, "Workspace");
    vbe_fill_rect_alpha(x + 12, panel_y + 128, 110, 1, UI_BORDER_SOFT, 255);
    vbe_draw_string(x + 14, panel_y + 144, "ITEMS", UI_TEXT_SUBTLE);
    vbe_draw_int(x + 88, panel_y + 144, item_count, UI_ACCENT_ALT);
    vbe_draw_string(x + 14, panel_y + panel_h - 44, "Status", UI_TEXT_SUBTLE);
    if (exp_selected >= 0 && dir_cache[exp_selected].flags != 0) {
        vbe_draw_string(x + 14, panel_y + panel_h - 28, dir_cache[exp_selected].name, UI_TEXT);
    } else {
        vbe_draw_string(x + 14, panel_y + panel_h - 28, "No selection", UI_TEXT_MUTED);
    }

    ui_draw_panel_flat(content_x, panel_y, content_w, panel_h, UI_RADIUS_MD, UI_SURFACE_1, 235, UI_BORDER_SOFT, 255);
    ui_draw_chip(content_x + 12, toolbar_y, 46, 20, UI_SURFACE_2, UI_TEXT, "Back");
    ui_draw_chip(content_x + 66, toolbar_y, 38, 20, UI_SURFACE_2, UI_TEXT, "Up");
    ui_draw_chip(content_x + 112, toolbar_y, 70, 20, UI_SURFACE_2, UI_TEXT_MUTED, "New File");
    ui_draw_chip(content_x + 190, toolbar_y, 84, 20, UI_SURFACE_2, UI_TEXT_MUTED, "New Folder");
    ui_draw_chip(content_x + 282, toolbar_y, 60, 20, UI_SURFACE_2, UI_TEXT_MUTED, "Rename");
    ui_draw_chip(content_x + 350, toolbar_y, 54, 20, UI_SURFACE_2, UI_DANGER, "Delete");
    ui_draw_chip(content_x + content_w - 76, toolbar_y, 60, 20, UI_SURFACE_2, UI_TEXT_MUTED, "Refresh");
    vbe_fill_rect_alpha(content_x + 12, panel_y + 36, content_w - 24, 1, UI_BORDER_SOFT, 255);
    if (item_count == 0) {
        ui_draw_panel_flat(content_x + 18, list_y + 8, content_w - 36, 108, UI_RADIUS_MD, UI_SURFACE_0, 255, UI_BORDER_SOFT, 255);
        vbe_draw_vector_folder(content_x + 36, list_y + 32, 0);
        vbe_draw_string(content_x + 86, list_y + 42, "This directory is empty.", UI_TEXT);
        vbe_draw_string(content_x + 86, list_y + 60, "Create a file or folder to start using this workspace.", UI_TEXT_MUTED);
        vbe_draw_string(content_x + 86, list_y + 78, "Right click for actions.", UI_ACCENT_ALT);
        vbe_fill_rect_alpha(content_x + 12, panel_y + panel_h - 26, content_w - 24, 14, UI_SURFACE_0, 255);
        vbe_draw_string(content_x + 18, panel_y + panel_h - 22, "0 items", UI_TEXT_MUTED);
        return;
    }

    int row = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (dir_cache[i].flags != 0 && dir_cache[i].parent_index == current_dir) {
            ui_draw_list_card(content_x + 16, list_y + row * 54, content_w - 32, 44,
                              dir_cache[i].flags, dir_cache[i].name, (int)dir_cache[i].size, exp_selected == i);
            row++;
            if (list_y + row * 54 > panel_y + panel_h - 56) break;
        }
    }

    vbe_fill_rect_alpha(content_x + 12, panel_y + content_h - 26, content_w - 24, 14, UI_SURFACE_0, 255);
    vbe_draw_string(content_x + 18, panel_y + content_h - 22, "Ready", UI_TEXT_MUTED);
    vbe_draw_int(content_x + 72, panel_y + content_h - 22, item_count, UI_ACCENT_ALT);
    vbe_draw_string(content_x + 90, panel_y + content_h - 22, "items", UI_TEXT_SUBTLE);
    if (exp_selected >= 0 && dir_cache[exp_selected].flags != 0) {
        vbe_draw_string(content_x + content_w - 132, panel_y + content_h - 22, dir_cache[exp_selected].name, UI_TEXT);
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
    ui_draw_panel_flat(x, y, w, 28, UI_RADIUS_SM, UI_SURFACE_0, 235, UI_BORDER_SOFT, 255);
    ui_draw_chip(x + 8, y + 4, 44, 20, UI_SURFACE_2, UI_TEXT_MUTED, "Path");
    vbe_draw_string(x + 60, y + 9, path_buf, UI_TEXT);
}
void vbe_draw_narcpad(int x, int y, int w, int h, const char* title, const char* content) {
    (void)title;
    int text_x = x + 8;
    int text_y = y + 8;
    int text_w = w - 16;
    int text_h = h - 34;
    int line_y = text_y + 12;
    char line_buf[55];
    int char_idx = 0;
    ui_draw_panel_flat(x, y, w, h, UI_RADIUS_MD, UI_SURFACE_1, 235, UI_BORDER_SOFT, 255);
    ui_draw_app_toolbar(x, y, w, 0, 0);
    vbe_fill_rect_alpha(text_x, text_y, text_w, text_h, 0xFFFFFF, 255);
    vbe_draw_rect(text_x, text_y, text_w, text_h, 0xD9E2EC);
    while (*content && line_y < text_y + text_h - 16) {
        if (*content == '\n' || char_idx == 54) {
            line_buf[char_idx] = '\0';
            vbe_draw_string(text_x + 8, line_y, line_buf, UI_TEXT_DARK);
            line_y += 15;
            char_idx = 0;
            if (*content == '\n') { content++; continue; }
        }
        line_buf[char_idx++] = *content++;
    }
    line_buf[char_idx] = '\0';
    if (char_idx > 0) vbe_draw_string(text_x + 8, line_y, line_buf, UI_TEXT_DARK);
    
    extern uint32_t timer_ticks;
    if ((timer_ticks / 20) % 2 == 0) {
        int caret_x = text_x + 8 + char_idx * 9;
        vbe_fill_rect(caret_x, line_y, 2, 12, UI_ACCENT_DEEP);
    }
    vbe_fill_rect_alpha(text_x, y + h - 20, text_w, 12, 0xEEF3F8, 255);
    vbe_draw_string(text_x + text_w - 92, y + h - 18, "Ctrl+S Save", UI_TEXT_SUBTLE);
}
void vbe_draw_snake_game(int x, int y, int w, int h, int* px, int* py, int len, int ax, int ay, int dead, int score, int best) {
    int toolbar_y = y + 4;
    int board_w = 39 * 9 + 12;
    int board_h = 29 * 9 + 12;
    int board_x = x + (w - board_w) / 2;
    int board_y = y + 34 + ((h - 34 - board_h) > 0 ? (h - 34 - board_h) / 2 : 0);
    int game_over_x = x + (w - 84) / 2;
    int reset_text_x = x + (w - 128) / 2;
    if (board_x < x + 8) board_x = x + 8;
    if (board_y < y + 30) board_y = y + 30;
    ui_draw_app_toolbar(x, y, w, 0, 0);
    ui_draw_panel_flat(x, y, w, h, UI_RADIUS_MD, UI_SURFACE_1, 235, UI_BORDER_SOFT, 255);
    ui_draw_chip(x + 12, toolbar_y + 2, 62, 18, UI_SURFACE_2, UI_SUCCESS, "Score");
    vbe_draw_int(x + 82, toolbar_y + 7, score, UI_TEXT);
    ui_draw_chip(x + 118, toolbar_y + 2, 52, 18, UI_SURFACE_2, UI_TEXT_MUTED, "Best");
    vbe_draw_int(x + 178, toolbar_y + 7, best, UI_TEXT);
    ui_draw_chip(x + w - 70, toolbar_y + 2, 58, 18, UI_SURFACE_2, UI_TEXT_MUTED, "Reset");
    vbe_fill_rect(board_x, board_y, board_w, board_h, 0x10171F);
    vbe_draw_rect(board_x, board_y, board_w, board_h, UI_BORDER_SOFT);
    if (dead) {
        vbe_draw_string(game_over_x, board_y + 112, "GAME OVER", UI_DANGER);
        vbe_draw_string(reset_text_x, board_y + 134, "Press R to Reset", UI_TEXT_MUTED);
    } else {
        vbe_fill_rect(board_x + 6 + ax * 9, board_y + 6 + ay * 9, 7, 7, UI_DANGER);
        for (int i = 0; i < len; i++) {
            uint32_t col = (i == 0) ? UI_SUCCESS : 0x37B24D;
            vbe_fill_rect(board_x + 6 + px[i] * 9, board_y + 6 + py[i] * 9, 7, 7, col);
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
                vbe_blit_window(&windows[i], window_buffer, is_focused);
                break;
            case WIN_TYPE_EXPLORER:
                vbe_blit_window(&windows[i], NULL, is_focused);
                extern int exp_dir;
                {
                    int cx, cy, cw, ch;
                    vbe_get_window_client_rect(&windows[i], &cx, &cy, &cw, &ch);
                    vbe_draw_breadcrumb(cx, cy, cw, exp_dir);
                    vbe_draw_explorer_content(cx, cy + 36, cw, ch - 36, exp_dir);
                }
                break;
            case WIN_TYPE_NARCPAD:
                vbe_blit_window(&windows[i], NULL, is_focused);
                extern char pad_content[1024];
                {
                    int cx, cy, cw, ch;
                    vbe_get_window_client_rect(&windows[i], &cx, &cy, &cw, &ch);
                    vbe_draw_narcpad(cx, cy, cw, ch, windows[i].title, pad_content);
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
                    vbe_get_window_client_rect(&windows[i], &cx, &cy, &cw, &ch);
                    rtc_format_timezone(tz_str, sizeof(tz_str));

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

                    ui_draw_panel_flat(cx, cy, cw, ch, UI_RADIUS_MD, UI_SURFACE_1, 235, UI_BORDER_SOFT, 255);
                    ui_draw_app_toolbar(cx, cy, cw, 0, 0);

                    ui_draw_panel_flat(cx + 16, cy + 12, cw - 32, 80, UI_RADIUS_MD, UI_SURFACE_0, 255, UI_BORDER_SOFT, 255);
                    vbe_draw_string(cx + 28, cy + 24, "LOCAL TIME", UI_TEXT_SUBTLE);
                    vbe_draw_string_hd(cx + 28, cy + 42, time_str, UI_TEXT, 2);
                    vbe_draw_string(cx + cw - 164, cy + 24, "DATE", UI_TEXT_SUBTLE);
                    vbe_draw_string(cx + cw - 164, cy + 44, date_str, UI_ACCENT_ALT);
                    ui_draw_chip(cx + cw - 164, cy + 62, 112, 20, UI_SURFACE_2, UI_TEXT, tz_str);

                    ui_draw_panel_flat(cx + 16, cy + 104, cw - 32, ch - 120, UI_RADIUS_MD, UI_SURFACE_0, 255, UI_BORDER_SOFT, 255);
                    vbe_draw_string(cx + 28, cy + 118, "TIME ZONE", UI_TEXT);
                    vbe_draw_string(cx + 28, cy + 136, "Current", UI_TEXT_SUBTLE);
                    ui_draw_chip(cx + 92, cy + 130, 112, 22, UI_ACCENT_DEEP, UI_TEXT, tz_str);
                    ui_draw_chip(cx + 216, cy + 130, 62, 22, UI_SURFACE_2, UI_TEXT, "-30m");
                    ui_draw_chip(cx + 286, cy + 130, 62, 22, UI_SURFACE_2, UI_TEXT, "+30m");
                    ui_draw_chip(cx + 360, cy + 130, 126, 22, UI_SURFACE_2, UI_TEXT_MUTED, "Edit Config");

                    vbe_draw_string(cx + 28, cy + 166, "PRESETS", UI_TEXT_SUBTLE);
                    ui_draw_chip(cx + 24, cy + 180, 68, 22, offset == -300 ? UI_ACCENT_DEEP : UI_SURFACE_2, UI_TEXT, "UTC-5");
                    ui_draw_chip(cx + 102, cy + 180, 48, 22, offset == 0 ? UI_ACCENT_DEEP : UI_SURFACE_2, UI_TEXT, "UTC");
                    ui_draw_chip(cx + 160, cy + 180, 68, 22, offset == 180 ? UI_ACCENT_DEEP : UI_SURFACE_2, UI_TEXT, "UTC+3");
                    ui_draw_chip(cx + 238, cy + 180, 96, 22, offset == 330 ? UI_ACCENT_DEEP : UI_SURFACE_2, UI_TEXT, "UTC+5:30");
                    ui_draw_chip(cx + 344, cy + 180, 68, 22, offset == 540 ? UI_ACCENT_DEEP : UI_SURFACE_2, UI_TEXT, "UTC+9");

                    vbe_draw_string(cx + 28, cy + 220, "Offset", UI_TEXT_SUBTLE);
                    vbe_draw_int(cx + 84, cy + 220, offset, UI_ACCENT_ALT);
                    vbe_draw_string(cx + 124, cy + 220, "min", UI_TEXT_MUTED);
                    vbe_draw_string(cx + 28, cy + 238, "/system/timezone.cfg", UI_TEXT_MUTED);
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
