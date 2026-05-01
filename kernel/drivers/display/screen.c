#include <stdint.h>
#include "io.h"
#include "vbe.h"
#include "ui_theme.h"
#include "string.h"

#define WIN_WIDTH 700
#define WIN_HEIGHT 475

#define TERM_CANVAS_BG      UI_SURFACE_0
#define TERM_PANEL_BG       UI_SURFACE_0
#define TERM_PANEL_ALT      UI_SURFACE_0
#define TERM_TEXT           UI_TEXT
#define TERM_TEXT_MUTED     UI_TEXT_MUTED
#define TERM_TEXT_SUBTLE    UI_TEXT_SUBTLE
#define TERM_PROMPT_USER    0x7FDB7F
#define TERM_PROMPT_PATH    0x6EC7FF
#define TERM_ACCENT         UI_ACCENT
#define TERM_WARN           UI_WARNING
#define TERM_ERR            UI_DANGER

#define TERM_CONTENT_X      24
#define TERM_CONTENT_Y      46
#define TERM_CELL_W         7
#define TERM_CELL_H         12
#define TERM_MAX_COLS       160
#define TERM_SCROLLBACK_LINES 512
#define VGA_TEXT_COLS       80
#define VGA_TEXT_ROWS       25
#define VGA_TEXT_BASE       ((volatile uint16_t*)0xB8000)

typedef struct {
    uint16_t glyph;
    uint8_t color;
    uint8_t reserved;
    uint32_t rgb;
} screen_char_t;

static int win_x = 150;
static int win_y = 120;
int win_visible = 0;
static int cursor_x = 0;
static int cursor_y = 0;
static int screen_graphics_enabled = 0;
static int vga_window_dirty = 1;
static int vga_last_window_w = 0;
static int vga_last_window_h = 0;
/* Keep a longer terminal history so the window can scroll back. */
static screen_char_t text_buffer[TERM_SCROLLBACK_LINES][TERM_MAX_COLS];
static int term_line_count = 1;
static int term_view_scroll = 0;

extern volatile uint32_t timer_ticks;
extern window_t windows[MAX_WINDOWS];
extern int nwm_get_idx_by_type(window_type_t type);

static const uint32_t term_palette[16] = {
    0x000000, 0x4FA3FF, TERM_PROMPT_USER, 0x52D1DC,
    0xE06C75, 0xC678DD, 0xD19A66, TERM_TEXT,
    TERM_TEXT_MUTED, TERM_PROMPT_PATH, TERM_PROMPT_USER, 0x88C0FF,
    TERM_ERR, 0xFF7AF6, TERM_WARN, 0xFFFFFF
};

static const screen_char_t term_blank_cell = {
    0, 0x07, 0, TERM_TEXT
};

static int term_window_w(void) {
    int idx = nwm_get_idx_by_type(WIN_TYPE_TERMINAL);
    if (idx >= 0 && windows[idx].w > 0) return windows[idx].w;
    return WIN_WIDTH;
}

static int term_window_h(void) {
    int idx = nwm_get_idx_by_type(WIN_TYPE_TERMINAL);
    if (idx >= 0 && windows[idx].h > 0) return windows[idx].h;
    return WIN_HEIGHT;
}

static int term_content_w(void) {
    int w = term_window_w() - 48;
    if (w < TERM_CELL_W * 8) w = TERM_CELL_W * 8;
    return w;
}

static int term_content_h(void) {
    int h = term_window_h() - 68;
    if (h < TERM_CELL_H * 4) h = TERM_CELL_H * 4;
    return h;
}

static int term_cols_visible(void) {
    int cols = term_content_w() / TERM_CELL_W;
    if (cols < 8) cols = 8;
    if (cols > TERM_MAX_COLS) cols = TERM_MAX_COLS;
    return cols;
}

static int term_rows_visible(void) {
    int rows = term_content_h() / TERM_CELL_H;
    if (rows < 4) rows = 4;
    return rows;
}

static int term_max_scroll(void);

static void term_ensure_layout_valid(void) {
    int cols = term_cols_visible();
    int max_scroll;

    if (cursor_x >= cols) cursor_x = cols - 1;
    if (cursor_x < 0) cursor_x = 0;
    if (cursor_y < 0) cursor_y = 0;
    if (cursor_y >= TERM_SCROLLBACK_LINES) cursor_y = TERM_SCROLLBACK_LINES - 1;
    if (term_line_count < 1) term_line_count = 1;
    if (term_line_count > TERM_SCROLLBACK_LINES) term_line_count = TERM_SCROLLBACK_LINES;
    max_scroll = term_max_scroll();
    if (term_view_scroll > max_scroll) term_view_scroll = max_scroll;
    if (term_view_scroll < 0) term_view_scroll = 0;
}

static void term_clear_line(int line) {
    if (line < 0 || line >= TERM_SCROLLBACK_LINES) return;
    for (int x = 0; x < TERM_MAX_COLS; x++) {
        text_buffer[line][x] = term_blank_cell;
    }
}

static int term_max_scroll(void) {
    int rows = term_rows_visible();
    if (term_line_count <= rows) return 0;
    return term_line_count - rows;
}

static void term_clamp_view_scroll(void) {
    int max_scroll = term_max_scroll();
    if (term_view_scroll < 0) term_view_scroll = 0;
    if (term_view_scroll > max_scroll) term_view_scroll = max_scroll;
}

static int term_first_visible_line(void) {
    int first_line = term_line_count - term_rows_visible() - term_view_scroll;
    if (first_line < 0) first_line = 0;
    return first_line;
}

static int term_cursor_screen_y(void) {
    int first_line;
    int screen_y;

    if (term_view_scroll != 0) return -1;
    first_line = term_first_visible_line();
    screen_y = cursor_y - first_line;
    if (screen_y < 0 || screen_y >= term_rows_visible()) return -1;
    return screen_y;
}

static void term_shift_history_up(void) {
    for (int y = 0; y < TERM_SCROLLBACK_LINES - 1; y++) {
        memcpy(text_buffer[y], text_buffer[y + 1], sizeof(text_buffer[y]));
    }
    term_clear_line(TERM_SCROLLBACK_LINES - 1);
    if (cursor_y > 0) cursor_y--;
}

static void term_append_blank_line(void) {
    int preserve_view = (term_view_scroll > 0);

    if (term_line_count < TERM_SCROLLBACK_LINES) {
        cursor_y++;
        term_line_count = cursor_y + 1;
        term_clear_line(cursor_y);
    } else {
        term_shift_history_up();
        cursor_y = TERM_SCROLLBACK_LINES - 1;
        term_clear_line(cursor_y);
        term_line_count = TERM_SCROLLBACK_LINES;
    }

    if (preserve_view) term_view_scroll++;
    term_clamp_view_scroll();
}

static void textmode_update_cursor(void) {
    uint16_t pos = (uint16_t)(cursor_y * VGA_TEXT_COLS + cursor_x);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void textmode_put_cell(int x, int y, char c, uint8_t color) {
    if (x < 0 || x >= VGA_TEXT_COLS || y < 0 || y >= VGA_TEXT_ROWS) return;
    VGA_TEXT_BASE[y * VGA_TEXT_COLS + x] = ((uint16_t)color << 8) | (uint8_t)c;
}

static void textmode_scroll(void) {
    for (int y = 0; y < VGA_TEXT_ROWS - 1; y++) {
        for (int x = 0; x < VGA_TEXT_COLS; x++) {
            VGA_TEXT_BASE[y * VGA_TEXT_COLS + x] = VGA_TEXT_BASE[(y + 1) * VGA_TEXT_COLS + x];
        }
    }
    for (int x = 0; x < VGA_TEXT_COLS; x++) {
        textmode_put_cell(x, VGA_TEXT_ROWS - 1, ' ', 0x07);
    }
    cursor_y = VGA_TEXT_ROWS - 1;
}

void screen_set_graphics_enabled(int enabled) {
    screen_graphics_enabled = (enabled != 0);
}

int screen_is_graphics_enabled(void) {
    return screen_graphics_enabled;
}

static uint16_t term_map_codepoint(uint32_t cp) {
    switch (cp) {
        case 0x00C7: return 256;
        case 0x011E: return 257;
        case 0x0130: return 258;
        case 0x00D6: return 259;
        case 0x015E: return 260;
        case 0x00DC: return 261;
        case 0x00E7: return 262;
        case 0x011F: return 263;
        case 0x0131: return 264;
        case 0x00F6: return 265;
        case 0x015F: return 266;
        case 0x00FC: return 267;
        default:
            if (cp < 256) return (uint16_t)cp;
            return '?';
    }
}

static uint16_t term_utf8_next_glyph(const char** s) {
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
        return term_map_codepoint(cp);
    }
    if ((p[0] & 0xF0) == 0xE0 && p[1] != 0 && p[2] != 0) {
        cp = ((uint32_t)(p[0] & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (uint32_t)(p[2] & 0x3F);
        *s += 3;
        return term_map_codepoint(cp);
    }
    (*s)++;
    return '?';
}

static const char* term_glyph_utf8(uint16_t glyph) {
    switch (glyph) {
        case 256: return "\xC3\x87";
        case 257: return "\xC4\x9E";
        case 258: return "\xC4\xB0";
        case 259: return "\xC3\x96";
        case 260: return "\xC5\x9E";
        case 261: return "\xC3\x9C";
        case 262: return "\xC3\xA7";
        case 263: return "\xC4\x9F";
        case 264: return "\xC4\xB1";
        case 265: return "\xC3\xB6";
        case 266: return "\xC5\x9F";
        case 267: return "\xC3\xBC";
        default: return 0;
    }
}

static void term_draw_glyph(int x, int y, uint16_t glyph, uint32_t color) {
    const char* utf8 = term_glyph_utf8(glyph);
    if (utf8) vbe_draw_string(x, y, utf8, color);
    else vbe_draw_char(x, y, (char)(glyph & 0xFF), color);
}

static uint32_t term_color_to_rgb(uint8_t color) {
    return term_palette[color & 0x0F];
}

static void term_store_cell(screen_char_t* cell, uint16_t glyph, uint8_t color) {
    cell->glyph = glyph;
    cell->color = color;
    cell->rgb = term_color_to_rgb(color);
}

static void term_draw_shell(void) {
    int w = term_window_w();
    int h = term_window_h();
    vbe_fill_rect(0, 0, w, h, TERM_CANVAS_BG);
    vbe_fill_rect_alpha(0, 0, w, 32, UI_SURFACE_1, 255);
    vbe_fill_rect_alpha(0, 32, w, 1, UI_BORDER_SOFT, 255);
    vbe_fill_rect_alpha(0, 33, w, h - 33, UI_SURFACE_0, 255);
    vbe_draw_string(24, 14, "Terminal", UI_TEXT);
    vbe_draw_string(24, 48, "Shell", UI_TEXT_SUBTLE);
    vbe_draw_string(w - 92, 14, "PgUp/Dn", UI_TEXT_SUBTLE);
}

static void term_draw_cursor(void) {
    int screen_y;
    int px;
    int py;

    if (((timer_ticks / 25) % 2) != 0) return;
    screen_y = term_cursor_screen_y();
    if (screen_y < 0) return;
    px = TERM_CONTENT_X + cursor_x * TERM_CELL_W;
    py = TERM_CONTENT_Y + screen_y * TERM_CELL_H;
    vbe_fill_rect_alpha(px, py + 10, 8, 2, TERM_ACCENT, 255);
}

void vga_set_window_pos(int x, int y) {
    win_x = x;
    win_y = y;
}

int vga_get_window_x() { return win_x; }
int vga_get_window_y() { return win_y; }
int vga_get_window_w() { return term_window_w(); }
int vga_get_window_h() { return term_window_h(); }
int vga_get_title_h() { return 0; }
void* vga_get_window_buffer() { return vbe_get_window_buffer(); }

int vga_window_needs_refresh(void) {
    int w = term_window_w();
    int h = term_window_h();
    if (vga_window_dirty) return 1;
    if (w != vga_last_window_w || h != vga_last_window_h) return 1;
    return 0;
}

void vga_prepare_win_draw() {
    vbe_set_target(vbe_get_window_buffer(), (uint32_t)term_window_w());
}

void vga_redraw_text_to_buffer() {
    int first_line = term_first_visible_line();
    int rows = term_rows_visible();
    int cols = term_cols_visible();
    term_ensure_layout_valid();
    first_line = term_first_visible_line();

    for (int y = 0; y < rows; y++) {
        int line_idx = first_line + y;
        if (line_idx >= term_line_count) break;
        for (int x = 0; x < cols; x++) {
            if (text_buffer[line_idx][x].glyph != 0) {
                term_draw_glyph(TERM_CONTENT_X + x * TERM_CELL_W, TERM_CONTENT_Y + y * TERM_CELL_H,
                                text_buffer[line_idx][x].glyph, text_buffer[line_idx][x].rgb);
            }
        }
    }
    term_draw_cursor();
}

void draw_window_frame_to_buffer() {
    term_ensure_layout_valid();
    vga_prepare_win_draw();
    term_draw_shell();
}

void vga_refresh_window() {
    draw_window_frame_to_buffer();
    vga_redraw_text_to_buffer();
    vga_last_window_w = term_window_w();
    vga_last_window_h = term_window_h();
    vga_window_dirty = 0;
    vbe_set_target(vbe_get_backbuffer(), vbe_get_width());
    gui_needs_redraw = 1;
}

void vga_scroll() {
    if (!screen_graphics_enabled) {
        textmode_scroll();
        textmode_update_cursor();
        return;
    }
    term_append_blank_line();
    vga_refresh_window();
}

void clear_screen() {
    if (!screen_graphics_enabled) {
        for (int i = 0; i < VGA_TEXT_COLS * VGA_TEXT_ROWS; i++) {
            VGA_TEXT_BASE[i] = ((uint16_t)0x07 << 8) | ' ';
        }
        cursor_x = 0;
        cursor_y = 0;
        textmode_update_cursor();
        return;
    }
    for (int y = 0; y < TERM_SCROLLBACK_LINES; y++) {
        term_clear_line(y);
    }
    cursor_x = 0;
    cursor_y = 0;
    term_line_count = 1;
    term_view_scroll = 0;
    vga_refresh_window();
}

void vga_newline() {
    cursor_x = 0;
    cursor_y++;
    if (!screen_graphics_enabled) {
        if (cursor_y >= VGA_TEXT_ROWS) vga_scroll();
        textmode_update_cursor();
        return;
    }
    if (cursor_y >= term_line_count) {
        if (cursor_y > 0) cursor_y--;
        term_append_blank_line();
    }
    vga_refresh_window();
}

static void vga_put_glyph_color(uint16_t glyph, uint8_t color) {
    uint32_t rgb;
    char ch;
    if (glyph == '\n' || glyph == '\r') {
        vga_newline();
        return;
    }
    if (glyph == '\t') {
        for (int i = 0; i < 4; i++) vga_put_glyph_color(' ', color);
        return;
    }
    if (!screen_graphics_enabled) {
        if (cursor_x >= VGA_TEXT_COLS) vga_newline();
        ch = (glyph < 256) ? (char)(glyph & 0xFF) : '?';
        if ((uint8_t)ch < 32U) ch = '?';
        textmode_put_cell(cursor_x, cursor_y, ch, color);
        cursor_x++;
        if (cursor_x >= VGA_TEXT_COLS) vga_newline();
        else textmode_update_cursor();
        return;
    }
    if (cursor_x >= term_cols_visible() - 1) vga_newline();
    term_store_cell(&text_buffer[cursor_y][cursor_x], glyph, color);
    if (term_view_scroll == 0) {
        int screen_y = term_cursor_screen_y();
        if (screen_y >= 0) {
            rgb = text_buffer[cursor_y][cursor_x].rgb;
            vga_prepare_win_draw();
            term_draw_glyph(TERM_CONTENT_X + cursor_x * TERM_CELL_W, TERM_CONTENT_Y + screen_y * TERM_CELL_H, glyph, rgb);
            vbe_fill_rect_alpha(TERM_CONTENT_X + cursor_x * TERM_CELL_W, TERM_CONTENT_Y + screen_y * TERM_CELL_H + 10, 8, 2, TERM_PANEL_ALT, 255);
            cursor_x++;
            term_draw_cursor();
            vbe_set_target(vbe_get_backbuffer(), vbe_get_width());
            gui_needs_redraw = 1;
            return;
        }
    }
    cursor_x++;
    vga_refresh_window();
}

void vga_putchar_color(char c, uint8_t color) {
    vga_put_glyph_color((uint8_t)c, color);
}

void vga_putchar(char c) { vga_putchar_color(c, 0x07); }
void vga_print(const char* str) {
    while (*str) {
        uint16_t glyph = term_utf8_next_glyph(&str);
        if (glyph == 0) break;
        vga_put_glyph_color(glyph, 0x07);
    }
}
void vga_println(const char* str) { vga_print(str); vga_newline(); }
void vga_print_color(const char* str, uint8_t color) {
    while (*str) {
        uint16_t glyph = term_utf8_next_glyph(&str);
        if (glyph == 0) break;
        vga_put_glyph_color(glyph, color);
    }
}

void vga_backspace() {
    if (!screen_graphics_enabled) {
        if (cursor_x > 0) {
            cursor_x--;
        } else if (cursor_y > 0) {
            cursor_y--;
            cursor_x = VGA_TEXT_COLS - 1;
        } else {
            return;
        }
        textmode_put_cell(cursor_x, cursor_y, ' ', 0x07);
        textmode_update_cursor();
        return;
    }
    if (cursor_x > 0) {
        cursor_x--;
        text_buffer[cursor_y][cursor_x] = term_blank_cell;
        vga_refresh_window();
    }
}

void vga_scrollback_page(int direction) {
    if (!screen_graphics_enabled || direction == 0) return;
    term_view_scroll += direction * (term_rows_visible() - 2);
    term_clamp_view_scroll();
    vga_refresh_window();
}

void vga_scrollback_lines(int direction) {
    if (!screen_graphics_enabled || direction == 0) return;
    term_view_scroll += direction;
    term_clamp_view_scroll();
    vga_refresh_window();
}

void vga_scrollback_home(void) {
    if (!screen_graphics_enabled) return;
    term_view_scroll = term_max_scroll();
    vga_refresh_window();
}

void vga_scrollback_end(void) {
    if (!screen_graphics_enabled) return;
    if (term_view_scroll == 0) return;
    term_view_scroll = 0;
    vga_refresh_window();
}

void vga_scrollback_follow_live(void) {
    vga_scrollback_end();
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
    {
        char buf[16];
        int i = 0;
        while (num > 0) {
            buf[i++] = (char)((num % 10) + '0');
            num /= 10;
        }
        for (int j = i - 1; j >= 0; j--) vga_putchar(buf[j]);
    }
}
