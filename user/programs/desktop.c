#include "user_lib.h"
#define MAPLE_MONO_8X8_SYMBOL user_gui_font
#include "maple_mono_8x8.h"
#undef MAPLE_MONO_8X8_SYMBOL
#include "user_gui_lib.h"

#define TASKBAR_HEIGHT UI_TASKBAR_HEIGHT
#define START_BUTTON_X UI_TASKBAR_PAD_X
#define START_BUTTON_Y UI_TASKBAR_PAD_Y
#define START_BUTTON_W 108
#define START_BUTTON_H UI_TASKBAR_ITEM_H
#define TASK_SLOT_X (START_BUTTON_X + START_BUTTON_W + 10 + APP_BUTTON_W + UI_TASKBAR_GAP)
#define TASK_SLOT_Y UI_TASKBAR_PAD_Y
#define TASK_SLOT_W 118
#define TASK_SLOT_H UI_TASKBAR_ITEM_H
#define TASK_SLOT_GAP UI_TASKBAR_GAP
#define TASK_SLOT_MAX 8
#define MENU_X 10
#define MENU_Y (TASKBAR_HEIGHT + UI_SPACE_3)
#define MENU_W 260
#define MENU_ITEM_H UI_MENU_ITEM_H
#define MENU_ITEMS 7
#define MENU_HEADER_H UI_MENU_HEADER_H
#define MENU_SECTION_H UI_MENU_SECTION_H
#define MENU_PRIMARY_ITEMS 5
#define CTX_MENU_W 196
#define CTX_MENU_ITEM_H 26
#define CTX_MENU_ITEMS 4
#define WINDOW_CAPTION_H UI_WINDOW_TITLEBAR_H
#define WINDOW_EDGE UI_WINDOW_EDGE_SIZE
#define WINDOW_CLIENT_INSET_X 1
#define WINDOW_CLIENT_TOP UI_WINDOW_CLIENT_TOP
#define WINDOW_CLIENT_BOTTOM 8
#define WINDOW_CTRL_SIZE UI_WINDOW_CTRL_SIZE
#define WINDOW_CTRL_GAP UI_WINDOW_CTRL_GAP
#define WINDOW_CTRL_TOP UI_WINDOW_CTRL_TOP
#define WINDOW_CTRL_RIGHT_PAD UI_WINDOW_CTRL_PAD_R
#define WINDOW_SNAP_MARGIN 10
#define WINDOW_DRAG_VISIBLE_EDGE 80
#define RESIZE_LEFT 1
#define RESIZE_RIGHT 2
#define RESIZE_BOTTOM 4
#define DESKTOP_DOUBLE_CLICK_TICKS 70U
#define DESKTOP_LAUNCH_THROTTLE_TICKS 80U
#define DESKTOP_ICON_AREA_X 20
#define DESKTOP_ICON_AREA_Y 60
#define DESKTOP_ICON_CARD_W 92
#define DESKTOP_ICON_CARD_H 108
#define DESKTOP_ICON_GAP_X 16
#define DESKTOP_ICON_GAP_Y 16
#define DESKTOP_ICON_GLYPH_BOX 50
#define DESKTOP_ICON_GLYPH_SIZE 44
#define DESKTOP_ICON_LABEL_Y 72
#define DESKTOP_ICON_TEXT_MAX 11
#define DESKTOP_ICON_CACHE_TICKS 100U
#define CLOCK_BOX_W 118
#define CLOCK_BOX_H UI_TASKBAR_ITEM_H
#define APP_BUTTON_W 96
#define SETTINGS_BUTTON_W 96
#define TASKBAR_NET_W 92

#if defined(__x86_64__)
#define DESKTOP_BG_W 224
#define DESKTOP_BG_H 126
extern const uint8_t _binary_obj_x86_64_user_assets_desktop_bg_rgb_start[];
extern const uint8_t _binary_obj_x86_64_user_assets_desktop_bg_rgb_end[];
#else
#define DESKTOP_BG_W 320
#define DESKTOP_BG_H 180
extern const uint8_t _binary_obj_i386_user_assets_desktop_bg_rgb_start[];
extern const uint8_t _binary_obj_i386_user_assets_desktop_bg_rgb_end[];
#endif

typedef struct {
    const char* label;
    const char* subtitle;
} menu_item_t;

typedef struct {
    int window_id;
    uint32_t* pixels;
    uint32_t capacity_pixels;
    int width;
    int height;
    uint32_t flags;
} window_surface_cache_t;

#define DESKTOP_ICON_MAX 16

typedef struct {
    int kind;
    int node_idx;
    int x;
    int y;
    char label[32];
    char path[256];
    uint32_t accent;
} desktop_icon_entry_t;

typedef struct {
    desktop_icon_entry_t entries[DESKTOP_ICON_MAX];
    int count;
    int screen_w;
    int screen_h;
    uint32_t loaded_tick;
    int valid;
} desktop_icon_cache_t;

enum {
    DESKTOP_ICON_KIND_THIS_PC = 1,
    DESKTOP_ICON_KIND_FILE = 2,
    DESKTOP_ICON_KIND_DIR = 3,
    DESKTOP_ICON_KIND_SNAKE = 4,
    DESKTOP_ICON_KIND_SETTINGS = 5,
    DESKTOP_ICON_KIND_DOOM = 6
};

enum {
    WINDOW_CTRL_NONE = 0,
    WINDOW_CTRL_MINIMIZE = 1,
    WINDOW_CTRL_CLOSE = 2,
    WINDOW_CTRL_MAXIMIZE = 3
};

enum {
    WINDOW_LAYOUT_NONE = 0,
    WINDOW_LAYOUT_MAXIMIZED = 1,
    WINDOW_LAYOUT_SNAP_LEFT = 2,
    WINDOW_LAYOUT_SNAP_RIGHT = 3
};

static desktop_icon_cache_t desktop_icon_cache;
static uint32_t desktop_last_launch_tick;
static int desktop_last_launch_key;
static char desktop_last_launch_path[256];

static const menu_item_t menu_items[MENU_ITEMS] = {
    {"Open Explorer", "Browse the desktop folder"},
    {"Open Settings", "Launch the system settings app"},
    {"Run Snake", "Launch the Snake game"},
    {"Run Doom", "Launch the Doom port"},
    {"Open Readme", "Launch NarcPad with the desktop readme"},
    {"Run Hello", "Spawn /bin/hello"},
    {"Run Neofetch", "Spawn /bin/neofetch"}
};

static const char* context_menu_items[CTX_MENU_ITEMS] = {"New File", "New Folder", "Refresh", "Settings"};
static const int menu_item_icons[MENU_ITEMS] = {
    USER_GUI_ICON_EXPLORER,
    USER_GUI_ICON_SETTINGS,
    USER_GUI_ICON_SNAKE,
    USER_GUI_ICON_DOOM,
    USER_GUI_ICON_NARCPAD,
    USER_GUI_ICON_APP,
    USER_GUI_ICON_INFO
};

typedef struct {
    int window_id;
    int valid;
    int mode;
    int x;
    int y;
    int w;
    int h;
} window_layout_state_t;

static void copy_text(char* dst, size_t dst_size, const char* src);
static void copy_title_label(char* dst, size_t dst_size, const char* src);
static int hit_test_window_control(const gui_window_snapshot_entry_t* win, int mx, int my);
static int text_length(const char* src);
static int start_menu_height(void);
static int start_menu_item_y(int item_idx);
static void draw_string_clipped(user_gui_surface_t* surface, int x, int y, const char* text, uint32_t color,
                                int clip_x, int clip_y, int clip_w, int clip_h);
static void draw_tall_string_clipped(user_gui_surface_t* surface, int x, int y, const char* text,
                                     uint32_t color, uint32_t shadow,
                                     int clip_x, int clip_y, int clip_w, int clip_h);
static void copy_icon_label(char* dst, size_t dst_size, const char* src);
static void desktop_icon_rect(int slot, int screen_w, int screen_h, int* out_x, int* out_y);
static void desktop_icon_cache_invalidate(void);
static const desktop_icon_entry_t* desktop_icon_cache_get(int screen_w, int screen_h, int* out_count);
static void format_rate_short(char* out, uint32_t bytes_per_sec);
static void draw_taskbar_network_panel(user_gui_surface_t* surface, int x, int y, int w, int h);
static int rects_intersect(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh);
static int desktop_launch_is_throttled(int key, const char* path);

static void draw_surface_preview_clipped(user_gui_surface_t* dst, int x, int y, int w, int h,
                                         const uint32_t* src, int src_w, int src_h,
                                         int clip_x, int clip_y, int clip_w, int clip_h) {
    int x0;
    int y0;
    int x1;
    int y1;

    if (!dst || !src || w <= 0 || h <= 0 || src_w <= 0 || src_h <= 0 || clip_w <= 0 || clip_h <= 0) return;
    x0 = x > clip_x ? x : clip_x;
    y0 = y > clip_y ? y : clip_y;
    x1 = x + w < clip_x + clip_w ? x + w : clip_x + clip_w;
    y1 = y + h < clip_y + clip_h ? y + h : clip_y + clip_h;
    if (x0 >= x1 || y0 >= y1) return;
    if (w == src_w && h == src_h) {
        for (int py = y0; py < y1; py++) {
            const uint32_t* src_row = src + (size_t)(py - y) * (size_t)src_w + (size_t)(x0 - x);
            uint32_t* dst_row = dst->pixels + (size_t)py * (size_t)dst->width + (size_t)x0;

            for (int px = x0; px < x1; px++) {
                dst_row[px - x0] = src_row[px - x0];
            }
        }
        return;
    }
    for (int py = y0; py < y1; py++) {
        int sy = ((py - y) * src_h) / h;
        uint32_t* dst_row = dst->pixels + (size_t)py * (size_t)dst->width;
        const uint32_t* src_row = src + (size_t)sy * (size_t)src_w;

        for (int px = x0; px < x1; px++) {
            int sx = ((px - x) * src_w) / w;

            dst_row[px] = src_row[sx];
        }
    }
}

static uint32_t mix_color(uint32_t fg, uint32_t bg, int alpha) {
    return user_gui_mix_color(fg, bg, alpha);
}

static void fill_vertical_gradient(user_gui_surface_t* surface, int x, int y, int w, int h,
                                   uint32_t top, uint32_t bottom) {
    if (!surface || w <= 0 || h <= 0) return;
    for (int py = 0; py < h; py++) {
        int alpha = h > 1 ? (py * 255) / (h - 1) : 255;
        uint32_t row = mix_color(bottom, top, alpha);
        user_gui_fill_rect(surface, x, y + py, w, 1, row);
    }
}

static void fill_vertical_gradient_rounded(user_gui_surface_t* surface, int x, int y, int w, int h,
                                           int radius, uint32_t top, uint32_t bottom) {
    if (!surface || w <= 0 || h <= 0) return;
    if (radius <= 1) {
        fill_vertical_gradient(surface, x, y, w, h, top, bottom);
        return;
    }
    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;
    for (int py = 0; py < h; py++) {
        int alpha = h > 1 ? (py * 255) / (h - 1) : 255;
        uint32_t row = mix_color(bottom, top, alpha);
        int inset = 0;

        if (py < radius) {
            int dy = radius - py;
            int dx = radius - inset;
            while (inset < radius && dx * dx + dy * dy > radius * radius) {
                inset++;
                dx = radius - inset;
            }
        } else if (py >= h - radius) {
            int dy = py - (h - radius - 1);
            int dx = radius - inset;
            while (inset < radius && dx * dx + dy * dy > radius * radius) {
                inset++;
                dx = radius - inset;
            }
        }
        user_gui_fill_rect(surface, x + inset, y + py, w - inset * 2, 1, row);
    }
}

static void fill_vertical_gradient_top_rounded(user_gui_surface_t* surface, int x, int y, int w, int h,
                                               int radius, uint32_t top, uint32_t bottom) {
    if (!surface || w <= 0 || h <= 0) return;
    if (radius <= 1) {
        fill_vertical_gradient(surface, x, y, w, h, top, bottom);
        return;
    }
    if (radius * 2 > w) radius = w / 2;
    if (radius > h) radius = h;
    for (int py = 0; py < h; py++) {
        int alpha = h > 1 ? (py * 255) / (h - 1) : 255;
        uint32_t row = mix_color(bottom, top, alpha);
        int inset = 0;

        if (py < radius) {
            int dy = radius - py;
            int dx = radius - inset;
            while (inset < radius && dx * dx + dy * dy > radius * radius) {
                inset++;
                dx = radius - inset;
            }
        }
        user_gui_fill_rect(surface, x + inset, y + py, w - inset * 2, 1, row);
    }
}

static void fill_vertical_gradient_clipped(user_gui_surface_t* surface, int x, int y, int w, int h,
                                           uint32_t top, uint32_t bottom,
                                           int clip_x, int clip_y, int clip_w, int clip_h) {
    int y0;
    int y1;
    int x0;
    int x1;

    if (!surface || w <= 0 || h <= 0 || clip_w <= 0 || clip_h <= 0) return;
    x0 = x > clip_x ? x : clip_x;
    y0 = y > clip_y ? y : clip_y;
    x1 = x + w < clip_x + clip_w ? x + w : clip_x + clip_w;
    y1 = y + h < clip_y + clip_h ? y + h : clip_y + clip_h;
    if (x0 >= x1 || y0 >= y1) return;
    for (int py = y0; py < y1; py++) {
        int local_y = py - y;
        int alpha = h > 1 ? (local_y * 255) / (h - 1) : 255;
        uint32_t row = mix_color(bottom, top, alpha);
        user_gui_fill_rect(surface, x0, py, x1 - x0, 1, row);
    }
}

static void draw_soft_shadow(user_gui_surface_t* surface, int x, int y, int w, int h, int radius) {
    if (!surface) return;
    user_gui_draw_rounded_rect(surface, x + 4, y + 5, w, h, radius, UI_SHADOW, 92);
    user_gui_draw_rounded_rect(surface, x + 1, y + 2, w, h, radius, UI_SHADOW, 58);
}

static void draw_panel_elevated(user_gui_surface_t* surface, int x, int y, int w, int h,
                                int radius, uint32_t top, uint32_t bottom,
                                uint32_t border, uint32_t glow) {
    if (!surface) return;
    draw_soft_shadow(surface, x, y, w, h, radius);
    user_gui_draw_rounded_rect(surface, x, y, w, h, radius, border, 255);
    user_gui_draw_rounded_rect(surface, x + 1, y + 1, w - 2, h - 2,
                               radius > 1 ? radius - 1 : radius, bottom, 245);
    fill_vertical_gradient_rounded(surface, x + 1, y + 1, w - 2, h - 2,
                                   radius > 1 ? radius - 1 : radius, top, bottom);
    if (glow != 0U) {
        user_gui_fill_rect_alpha(surface, x + 14, y + 1, w - 28, 2, glow, 132);
    }
}

static void draw_panel_elevated_clipped(user_gui_surface_t* surface, int x, int y, int w, int h,
                                        int radius, uint32_t top, uint32_t bottom,
                                        uint32_t border, uint32_t glow,
                                        int clip_x, int clip_y, int clip_w, int clip_h) {
    if (!surface || clip_w <= 0 || clip_h <= 0) return;
    if (!rects_intersect(x, y, w, h, clip_x, clip_y, clip_w, clip_h) &&
        !rects_intersect(x + 1, y + 2, w + 3, h + 4, clip_x, clip_y, clip_w, clip_h)) {
        return;
    }
    if (clip_x >= x + 1 && clip_y >= y + 1 &&
        clip_x + clip_w <= x + w - 1 &&
        clip_y + clip_h <= y + h - 1) {
        fill_vertical_gradient_clipped(surface, x + 1, y + 1, w - 2, h - 2,
                                       top, bottom, clip_x, clip_y, clip_w, clip_h);
        return;
    }
    draw_panel_elevated(surface, x, y, w, h, radius, top, bottom, border, glow);
}

static void draw_xfce_button_clipped(user_gui_surface_t* surface, int x, int y, int w, int h,
                                     int radius, uint32_t top, uint32_t bottom, uint32_t border,
                                     int pressed,
                                     int clip_x, int clip_y, int clip_w, int clip_h) {
    if (!surface || clip_w <= 0 || clip_h <= 0) return;
    if (!rects_intersect(x, y, w, h, clip_x, clip_y, clip_w, clip_h)) return;
    user_gui_draw_rounded_rect(surface, x, y, w, h, radius, border, 255);
    fill_vertical_gradient_rounded(surface, x + 1, y + 1, w - 2, h - 2,
                                   radius > 1 ? radius - 1 : radius,
                                   pressed ? bottom : top, pressed ? top : bottom);
    user_gui_fill_rect_alpha(surface, x + 2, y + 2, w - 4, 1, UI_HILITE_SOFT, pressed ? 28 : 58);
    user_gui_fill_rect_alpha(surface, x + 1, y + h - 2, w - 2, 1, UI_SHADOW, pressed ? 62 : 38);
}

static void draw_window_control_button(user_gui_surface_t* surface, int x, int y, int control, int active) {
    uint32_t base;
    uint32_t border;
    uint32_t glyph;
    int s = WINDOW_CTRL_SIZE;

    if (!surface) return;
    switch (control) {
        case WINDOW_CTRL_CLOSE: base = active ? 0xD2645F : 0x555E68; break;
        default: base = active ? 0xE3E8ED : 0x555E68; break;
    }
    border = active ? mix_color(UI_HILITE_SOFT, base, 58) : 0x3D454E;
    glyph = active ? UI_TEXT_DARK : UI_TEXT_MUTED;

    user_gui_draw_rounded_rect(surface, x, y, s, s, 3, border, 255);
    fill_vertical_gradient_rounded(surface, x + 1, y + 1, s - 2, s - 2, 2,
                                   mix_color(UI_HILITE_SOFT, base, active ? 72 : 28), base);
    user_gui_fill_rect_alpha(surface, x + 2, y + s - 2, s - 4, 1, UI_SHADOW, active ? 46 : 70);

    if (control == WINDOW_CTRL_MINIMIZE) {
        user_gui_fill_rect(surface, x + 3, y + s - 4, s - 6, 2, glyph);
    } else if (control == WINDOW_CTRL_MAXIMIZE) {
        user_gui_draw_rect(surface, x + 3, y + 3, s - 6, s - 6, glyph);
        user_gui_fill_rect_alpha(surface, x + 4, y + 4, s - 8, 1, glyph, 150);
    } else if (control == WINDOW_CTRL_CLOSE) {
        for (int i = 0; i < s - 6; i++) {
            user_gui_fill_rect(surface, x + 3 + i, y + 3 + i, 2, 2, glyph);
            user_gui_fill_rect(surface, x + s - 4 - i, y + 3 + i, 2, 2, glyph);
        }
    }
}

static int desktop_icon_to_user_gui_icon(int kind) {
    switch (kind) {
        case DESKTOP_ICON_KIND_THIS_PC: return USER_GUI_ICON_EXPLORER;
        case DESKTOP_ICON_KIND_DIR: return USER_GUI_ICON_FOLDER;
        case DESKTOP_ICON_KIND_SETTINGS: return USER_GUI_ICON_SETTINGS;
        case DESKTOP_ICON_KIND_SNAKE: return USER_GUI_ICON_SNAKE;
        case DESKTOP_ICON_KIND_DOOM: return USER_GUI_ICON_DOOM;
        case DESKTOP_ICON_KIND_FILE: return USER_GUI_ICON_FILE;
        default: return USER_GUI_ICON_APP;
    }
}

static void draw_window_surface_card(user_gui_surface_t* surface, const gui_window_snapshot_entry_t* win,
                                     const window_surface_cache_t* cache,
                                     int clip_x, int clip_y, int clip_w, int clip_h) {
    int x;
    int y;
    int w;
    int h;
    int client_x;
    int client_y;
    int client_w;
    int client_h;

    if (!surface || !win || !cache || !cache->pixels || cache->width <= 0 || cache->height <= 0) return;
    if ((win->flags & GUI_WINDOW_SNAPSHOT_MINIMIZED) != 0U) return;
    x = win->x;
    y = win->y;
    w = win->width;
    h = win->height;
    client_x = x;
    client_y = y;
    client_w = w;
    client_h = h;

    if ((cache->flags & GUI_WINDOW_SURFACE_FLAG_FULL_WINDOW) != 0U) {
        draw_surface_preview_clipped(surface, x, y, w, h, cache->pixels, cache->width, cache->height,
                                     clip_x, clip_y, clip_w, clip_h);
        return;
    }

    if ((win->flags & GUI_WINDOW_SNAPSHOT_BORDERLESS) == 0U &&
        (win->flags & GUI_WINDOW_SNAPSHOT_FULLSCREEN) == 0U) {
        int active = (win->flags & GUI_WINDOW_SNAPSHOT_ACTIVE) != 0U;
        uint32_t top_fill = active ? UI_WINDOW_ACTIVE_TOP : UI_WINDOW_INACTIVE_TOP;
        uint32_t bottom_fill = active ? UI_WINDOW_ACTIVE_BOTTOM : UI_WINDOW_INACTIVE_BOTTOM;
        uint32_t title_top = active ? UI_WINDOW_ACTIVE_TOP : UI_WINDOW_INACTIVE_TOP;
        uint32_t title_bottom = active ? UI_WINDOW_ACTIVE_BOTTOM : UI_WINDOW_INACTIVE_BOTTOM;
        uint32_t title_text = active ? UI_TEXT : UI_TEXT_MUTED;

        draw_panel_elevated(surface, x, y, w, h, UI_RADIUS_MD, top_fill, bottom_fill,
                            active ? UI_BORDER_STRONG : UI_BORDER_SOFT, 0U);
        fill_vertical_gradient_top_rounded(surface, x + 1, y + 1, w - 2, WINDOW_CLIENT_TOP - 2,
                                           UI_RADIUS_MD > 1 ? UI_RADIUS_MD - 1 : UI_RADIUS_MD,
                                           title_top, title_bottom);
        user_gui_fill_rect_alpha(surface, x + 2, y + 2, w - 4, 1, UI_HILITE_SOFT, active ? 64 : 24);
        user_gui_fill_rect_alpha(surface, x + 1, y + WINDOW_CLIENT_TOP - 3, w - 2, 1,
                                 active ? 0x173E5E : UI_BORDER_SOFT, active ? 210 : 120);
        user_gui_draw_rounded_rect(surface, x + 12, y + 10, 6, 10, 2,
                                   active ? 0xD7EAF8 : UI_BORDER_STRONG, active ? 235 : 165);
        draw_tall_string_clipped(surface, x + 24, y + 8, win->title, title_text, UI_SHADOW,
                                 clip_x, clip_y, clip_w, clip_h);
        {
            int close_x = x + w - WINDOW_CTRL_RIGHT_PAD - WINDOW_CTRL_SIZE;
            int max_x = close_x - WINDOW_CTRL_GAP - WINDOW_CTRL_SIZE;
            int min_x = max_x - WINDOW_CTRL_GAP - WINDOW_CTRL_SIZE;

            draw_window_control_button(surface, min_x, y + WINDOW_CTRL_TOP, WINDOW_CTRL_MINIMIZE, active);
            draw_window_control_button(surface, max_x, y + WINDOW_CTRL_TOP, WINDOW_CTRL_MAXIMIZE, active);
            draw_window_control_button(surface, close_x, y + WINDOW_CTRL_TOP, WINDOW_CTRL_CLOSE, active);
        }
        client_x = x + WINDOW_CLIENT_INSET_X;
        client_y = y + WINDOW_CLIENT_TOP;
        client_w = w - WINDOW_CLIENT_INSET_X * 2;
        client_h = h - WINDOW_CLIENT_TOP - WINDOW_CLIENT_BOTTOM;
        if (clip_x >= client_x && clip_y >= client_y &&
            clip_x + clip_w <= client_x + client_w &&
            clip_y + clip_h <= client_y + client_h) {
            user_gui_fill_rect(surface, clip_x, clip_y, clip_w, clip_h, UI_WINDOW_CLIENT_BG);
            draw_surface_preview_clipped(surface, client_x, client_y, client_w, client_h,
                                         cache->pixels, cache->width, cache->height,
                                         clip_x, clip_y, clip_w, clip_h);
            return;
        }
        user_gui_fill_rect(surface, client_x, client_y, client_w, client_h, UI_WINDOW_CLIENT_BG);
    }

    draw_surface_preview_clipped(surface, client_x, client_y, client_w, client_h,
                                 cache->pixels, cache->width, cache->height,
                                 clip_x, clip_y, clip_w, clip_h);
}

static int point_in_rect(int px, int py, int x, int y, int w, int h) {
    return px >= x && py >= y && px < x + w && py < y + h;
}

static int rects_intersect(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    return aw > 0 && ah > 0 && bw > 0 && bh > 0 &&
           ax < bx + bw && ay < by + bh &&
           bx < ax + aw && by < ay + ah;
}

static void mark_dirty_rect(int* dirty_valid, int* dirty_x, int* dirty_y, int* dirty_w, int* dirty_h,
                            int x, int y, int w, int h, int screen_w, int screen_h) {
    int x2;
    int y2;

    if (!dirty_valid || !dirty_x || !dirty_y || !dirty_w || !dirty_h) return;
    if (w <= 0 || h <= 0 || screen_w <= 0 || screen_h <= 0) return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x >= screen_w || y >= screen_h) return;
    if (x + w > screen_w) w = screen_w - x;
    if (y + h > screen_h) h = screen_h - y;
    if (w <= 0 || h <= 0) return;
    x2 = x + w;
    y2 = y + h;
    if (!*dirty_valid) {
        *dirty_valid = 1;
        *dirty_x = x;
        *dirty_y = y;
        *dirty_w = w;
        *dirty_h = h;
        return;
    }
    if (x < *dirty_x) *dirty_x = x;
    if (y < *dirty_y) *dirty_y = y;
    if (x2 > *dirty_x + *dirty_w) *dirty_w = x2 - *dirty_x;
    if (y2 > *dirty_y + *dirty_h) *dirty_h = y2 - *dirty_y;
}

static void mark_dirty_full(int* dirty_valid, int* dirty_x, int* dirty_y, int* dirty_w, int* dirty_h,
                            int screen_w, int screen_h) {
    mark_dirty_rect(dirty_valid, dirty_x, dirty_y, dirty_w, dirty_h, 0, 0, screen_w, screen_h, screen_w, screen_h);
}

static void mark_taskbar_dirty(int* dirty_valid, int* dirty_x, int* dirty_y, int* dirty_w, int* dirty_h,
                               int screen_w, int screen_h) {
    mark_dirty_rect(dirty_valid, dirty_x, dirty_y, dirty_w, dirty_h,
                    0, 0, screen_w, TASKBAR_HEIGHT + 2, screen_w, screen_h);
}

static void mark_clock_dirty(int* dirty_valid, int* dirty_x, int* dirty_y, int* dirty_w, int* dirty_h,
                             int screen_w, int screen_h) {
    mark_dirty_rect(dirty_valid, dirty_x, dirty_y, dirty_w, dirty_h,
                    screen_w - CLOCK_BOX_W - 16, 6, CLOCK_BOX_W + 8, CLOCK_BOX_H + 4, screen_w, screen_h);
}

static void mark_menu_dirty(int* dirty_valid, int* dirty_x, int* dirty_y, int* dirty_w, int* dirty_h,
                            int screen_w, int screen_h) {
    int menu_h = start_menu_height();

    mark_dirty_rect(dirty_valid, dirty_x, dirty_y, dirty_w, dirty_h,
                    MENU_X - 6, MENU_Y - 6, MENU_W + 12, menu_h + 12, screen_w, screen_h);
}

static void mark_context_dirty(int* dirty_valid, int* dirty_x, int* dirty_y, int* dirty_w, int* dirty_h,
                               int screen_w, int screen_h, int context_x, int context_y) {
    int menu_h = 12 + CTX_MENU_ITEMS * CTX_MENU_ITEM_H + 10;

    mark_dirty_rect(dirty_valid, dirty_x, dirty_y, dirty_w, dirty_h,
                    context_x - 6, context_y - 6, CTX_MENU_W + 12, menu_h + 12, screen_w, screen_h);
}

static void mark_desktop_icon_entry_dirty(int* dirty_valid, int* dirty_x, int* dirty_y, int* dirty_w, int* dirty_h,
                                          int screen_w, int screen_h, const desktop_icon_entry_t* icon) {
    if (!icon) return;
    mark_dirty_rect(dirty_valid, dirty_x, dirty_y, dirty_w, dirty_h,
                    icon->x - 10, icon->y - 10, DESKTOP_ICON_CARD_W + 20, DESKTOP_ICON_CARD_H + 20,
                    screen_w, screen_h);
}

static void copy_text(char* dst, size_t dst_size, const char* src) {
    size_t i = 0;

    if (!dst || dst_size == 0U) return;
    if (!src) src = "";
    while (src[i] != '\0' && i + 1U < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int text_length(const char* src) {
    int len = 0;

    if (!src) return 0;
    while (src[len] != '\0') len++;
    return len;
}

static int start_menu_height(void) {
    return MENU_HEADER_H + MENU_SECTION_H * 2 + MENU_ITEMS * MENU_ITEM_H + 14;
}

static int start_menu_item_y(int item_idx) {
    int y = MENU_Y + MENU_HEADER_H + MENU_SECTION_H;

    if (item_idx >= MENU_PRIMARY_ITEMS) y += MENU_SECTION_H;
    return y + item_idx * MENU_ITEM_H;
}

static int append_text(char* dst, size_t dst_size, const char* src) {
    size_t len = 0;
    size_t i = 0;

    if (!dst || dst_size == 0U) return -1;
    if (!src) src = "";
    while (dst[len] != '\0' && len < dst_size) len++;
    if (len >= dst_size) return -1;
    while (src[i] != '\0') {
        if (len + 1U >= dst_size) return -1;
        dst[len++] = src[i++];
    }
    dst[len] = '\0';
    return 0;
}

static int append_uint(char* dst, size_t dst_size, uint32_t value) {
    char digits[12];
    int count = 0;

    if (!dst || dst_size == 0U) return -1;
    if (value == 0U) return append_text(dst, dst_size, "0");
    while (value != 0U && count < (int)sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (count > 0) {
        char ch[2];

        ch[0] = digits[--count];
        ch[1] = '\0';
        if (append_text(dst, dst_size, ch) != 0) return -1;
    }
    return 0;
}

static void draw_string_clipped(user_gui_surface_t* surface, int x, int y, const char* text, uint32_t color,
                                int clip_x, int clip_y, int clip_w, int clip_h) {
    int text_w;

    if (!surface || !text || text[0] == '\0' || clip_w <= 0 || clip_h <= 0) return;
    text_w = text_length(text) * 8;
    if (!rects_intersect(x - 1, y - 1, text_w + 2, 10, clip_x, clip_y, clip_w, clip_h)) return;
    user_gui_draw_string(surface, x, y, text, color);
}

static void draw_tall_string_clipped(user_gui_surface_t* surface, int x, int y, const char* text,
                                     uint32_t color, uint32_t shadow,
                                     int clip_x, int clip_y, int clip_w, int clip_h) {
    int text_w;

    if (!surface || !text || text[0] == '\0' || clip_w <= 0 || clip_h <= 0) return;
    text_w = text_length(text) * 9;
    if (!rects_intersect(x - 1, y - 1, text_w + 3, 18, clip_x, clip_y, clip_w, clip_h)) return;
    user_gui_draw_string_tall_shadow(surface, x, y, text, color, shadow);
}

static void copy_icon_label(char* dst, size_t dst_size, const char* src) {
    int len;

    if (!dst || dst_size == 0U) return;
    if (!src) src = "";
    len = text_length(src);
    if (len <= DESKTOP_ICON_TEXT_MAX) {
        copy_text(dst, dst_size, src);
        return;
    }
    if (dst_size < 5U) {
        copy_text(dst, dst_size, src);
        return;
    }
    for (int i = 0; i < DESKTOP_ICON_TEXT_MAX - 1 && i + 4 < (int)dst_size; i++) {
        dst[i] = src[i];
    }
    dst[DESKTOP_ICON_TEXT_MAX - 1] = '.';
    dst[DESKTOP_ICON_TEXT_MAX] = '.';
    dst[DESKTOP_ICON_TEXT_MAX + 1] = '.';
    dst[DESKTOP_ICON_TEXT_MAX + 2] = '\0';
}

static void desktop_icon_rect(int slot, int screen_w, int screen_h, int* out_x, int* out_y) {
    int usable_w;
    int cols;
    int col;
    int row;
    int x;
    int y;

    if (out_x) *out_x = DESKTOP_ICON_AREA_X;
    if (out_y) *out_y = DESKTOP_ICON_AREA_Y;
    if (slot < 0) return;
    usable_w = screen_w - DESKTOP_ICON_AREA_X * 2;
    cols = usable_w / (DESKTOP_ICON_CARD_W + DESKTOP_ICON_GAP_X);
    if (cols < 1) cols = 1;
    col = slot / 6;
    row = slot % 6;
    if (col >= cols) {
        col = slot % cols;
        row = slot / cols;
    }
    x = DESKTOP_ICON_AREA_X + col * (DESKTOP_ICON_CARD_W + DESKTOP_ICON_GAP_X);
    y = DESKTOP_ICON_AREA_Y + row * (DESKTOP_ICON_CARD_H + DESKTOP_ICON_GAP_Y);
    if (y + DESKTOP_ICON_CARD_H > screen_h - 56) {
        y = DESKTOP_ICON_AREA_Y + (slot % 6) * (DESKTOP_ICON_CARD_H + DESKTOP_ICON_GAP_Y);
    }
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
}

static void desktop_log(const char* message) {
    (void)message;
}

static const uint8_t* desktop_background_rgb(size_t* out_bytes, int* out_w, int* out_h) {
    const uint8_t* start;
    const uint8_t* end;

    if (out_bytes) *out_bytes = 0U;
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
#if defined(__x86_64__)
    start = _binary_obj_x86_64_user_assets_desktop_bg_rgb_start;
    end = _binary_obj_x86_64_user_assets_desktop_bg_rgb_end;
#else
    start = _binary_obj_i386_user_assets_desktop_bg_rgb_start;
    end = _binary_obj_i386_user_assets_desktop_bg_rgb_end;
#endif
    if (!start || !end || end <= start) return 0;
    if (out_bytes) *out_bytes = (size_t)(end - start);
    if (out_w) *out_w = DESKTOP_BG_W;
    if (out_h) *out_h = DESKTOP_BG_H;
    return start;
}

static void draw_desktop_background_region(user_gui_surface_t* surface, int clip_x, int clip_y, int clip_w, int clip_h) {
    const uint8_t* rgb;
    size_t rgb_bytes;
    int src_w;
    int src_h;
    int x0;
    int y0;
    int x1;
    int y1;

    if (!surface || !surface->pixels || surface->width <= 0 || surface->height <= 0) return;
    x0 = clip_x < 0 ? 0 : clip_x;
    y0 = clip_y < 0 ? 0 : clip_y;
    x1 = clip_x + clip_w;
    y1 = clip_y + clip_h;
    if (x1 > surface->width) x1 = surface->width;
    if (y1 > surface->height) y1 = surface->height;
    if (x0 >= x1 || y0 >= y1) return;
    rgb = desktop_background_rgb(&rgb_bytes, &src_w, &src_h);
    if (!rgb || src_w <= 0 || src_h <= 0 || rgb_bytes < (size_t)(src_w * src_h * 3)) {
        user_gui_fill_rect(surface, x0, y0, x1 - x0, y1 - y0, 0x0D141B);
        return;
    }

    if (surface->width == src_w && surface->height == src_h) {
        for (int y = y0; y < y1; y++) {
            for (int x = x0; x < x1; x++) {
                const uint8_t* px = rgb + (((size_t)y * (size_t)src_w) + (size_t)x) * 3U;
                surface->pixels[y * surface->width + x] =
                    ((uint32_t)px[0] << 16) | ((uint32_t)px[1] << 8) | (uint32_t)px[2];
            }
        }
        return;
    }

    {
        uint32_t x_step = ((uint32_t)src_w << 16) / (uint32_t)surface->width;
        uint32_t y_step = ((uint32_t)src_h << 16) / (uint32_t)surface->height;

        if (x_step == 0U) x_step = 1U;
        if (y_step == 0U) y_step = 1U;
        for (int y = y0; y < y1; y++) {
            uint32_t sy = ((uint32_t)y * y_step) >> 16;
            const uint8_t* src_row;
            uint32_t* dst_row;

            if (sy >= (uint32_t)src_h) sy = (uint32_t)src_h - 1U;
            src_row = rgb + (size_t)sy * (size_t)src_w * 3U;
            dst_row = surface->pixels + (size_t)y * (size_t)surface->width;
            for (int x = x0; x < x1; x++) {
                uint32_t sx = ((uint32_t)x * x_step) >> 16;
                const uint8_t* px;

                if (sx >= (uint32_t)src_w) sx = (uint32_t)src_w - 1U;
                px = src_row + (size_t)sx * 3U;
                dst_row[x] = ((uint32_t)px[0] << 16) | ((uint32_t)px[1] << 8) | (uint32_t)px[2];
            }
        }
    }
}

static void format_clock_text(char* dst, size_t dst_size) {
    rtc_local_time_t now;
    int hour_tens;
    int hour_ones;
    int minute_tens;
    int minute_ones;
    int second_tens;
    int second_ones;

    if (!dst || dst_size < 9U) return;
    if (user_get_local_time(&now) != 0) {
        copy_text(dst, dst_size, "--:--:--");
        return;
    }
    hour_tens = (int)(now.hour / 10U);
    hour_ones = (int)(now.hour % 10U);
    minute_tens = (int)(now.minute / 10U);
    minute_ones = (int)(now.minute % 10U);
    second_tens = (int)(now.second / 10U);
    second_ones = (int)(now.second % 10U);
    dst[0] = (char)('0' + hour_tens);
    dst[1] = (char)('0' + hour_ones);
    dst[2] = ':';
    dst[3] = (char)('0' + minute_tens);
    dst[4] = (char)('0' + minute_ones);
    dst[5] = ':';
    dst[6] = (char)('0' + second_tens);
    dst[7] = (char)('0' + second_ones);
    dst[8] = '\0';
}

static void draw_desktop_icon(user_gui_surface_t* surface, int kind, int x, int y, const char* label,
                              uint32_t accent, int hovered) {
    int card_x = x + 4;
    int card_y = y + 2;
    int card_w = DESKTOP_ICON_CARD_W - 8;
    int card_h = 66;
    int glyph_box_x;
    int glyph_box_y = y + 10;
    int glyph_x;
    int glyph_y;
    int label_len;
    int label_x;
    int label_chip_w;
    int label_chip_x;
    uint32_t hover_blue = 0x4F8CC9;
    uint32_t card_top = mix_color(hover_blue, 0x15202A, 86);
    uint32_t card_bottom = mix_color(0x16212C, hover_blue, 38);

    if (!surface) return;
    {
        uint32_t glyph_top = hovered ? mix_color(hover_blue, UI_SURFACE_2, 96) : 0x1A2530;
        uint32_t glyph_bottom = hovered ? mix_color(UI_SURFACE_2, hover_blue, 62) : 0x111A22;
        uint32_t label_fill = hovered ? mix_color(hover_blue, UI_SURFACE_2, 52) : 0x0F171F;
        uint32_t label_border = hovered ? mix_color(0xFFFFFF, hover_blue, 74) : 0x33404D;
        uint32_t label_text = hovered ? UI_TEXT : UI_TEXT_MUTED;
        uint32_t frame_border = mix_color(0xFFFFFF, hover_blue, 80);

        if (hovered) {
            draw_panel_elevated(surface, card_x, card_y, card_w, card_h, UI_RADIUS_LG,
                                card_top, card_bottom, frame_border, hover_blue);
            user_gui_fill_rect_alpha(surface, card_x + 12, card_y + 8, card_w - 24, 2, 0xFFFFFF, 84);
            user_gui_fill_rect_alpha(surface, card_x + 12, card_y + card_h - 12, card_w - 24, 1, hover_blue, 74);
        }

        glyph_box_x = x + (DESKTOP_ICON_CARD_W - DESKTOP_ICON_GLYPH_BOX) / 2;
        if (hovered) {
            draw_panel_elevated(surface, glyph_box_x, glyph_box_y,
                                DESKTOP_ICON_GLYPH_BOX, DESKTOP_ICON_GLYPH_BOX, UI_RADIUS_MD,
                                glyph_top, glyph_bottom, mix_color(0xFFFFFF, hover_blue, 90), hover_blue);
            user_gui_fill_rect_alpha(surface, glyph_box_x + 8, glyph_box_y + 7, DESKTOP_ICON_GLYPH_BOX - 16, 2, 0xFFFFFF, 96);
        } else {
            user_gui_draw_rounded_rect(surface, glyph_box_x, glyph_box_y,
                                       DESKTOP_ICON_GLYPH_BOX, DESKTOP_ICON_GLYPH_BOX, UI_RADIUS_MD,
                                       UI_SURFACE_0, 92);
            user_gui_fill_rect_alpha(surface, glyph_box_x + 8, glyph_box_y + 7,
                                     DESKTOP_ICON_GLYPH_BOX - 16, 1, UI_HILITE_SOFT, 26);
        }
        glyph_x = glyph_box_x + (DESKTOP_ICON_GLYPH_BOX - DESKTOP_ICON_GLYPH_SIZE) / 2;
        glyph_y = glyph_box_y + (DESKTOP_ICON_GLYPH_BOX - DESKTOP_ICON_GLYPH_SIZE) / 2;
        if (hovered) glyph_y -= 1;
        user_gui_draw_icon(surface, desktop_icon_to_user_gui_icon(kind), glyph_x, glyph_y,
                           DESKTOP_ICON_GLYPH_SIZE, accent, hovered);

        label_len = text_length(label);
        label_chip_w = label_len * 8 + 18;
        if (label_chip_w < 58) label_chip_w = 58;
        if (label_chip_w > DESKTOP_ICON_CARD_W - 8) label_chip_w = DESKTOP_ICON_CARD_W - 8;
        label_chip_x = x + (DESKTOP_ICON_CARD_W - label_chip_w) / 2;
        if (hovered) {
            draw_panel_elevated(surface, label_chip_x, y + DESKTOP_ICON_LABEL_Y, label_chip_w, 24, UI_RADIUS_SM,
                                mix_color(label_fill, 0x17222D, 120),
                                label_fill, label_border, hover_blue);
        }
        label_x = label_chip_x + (label_chip_w - label_len * 8) / 2;
        if (label_x < label_chip_x + 9) label_x = label_chip_x + 9;
        draw_string_clipped(surface, label_x + 1, y + DESKTOP_ICON_LABEL_Y + 9, label ? label : "",
                            UI_SHADOW, x, y, DESKTOP_ICON_CARD_W, DESKTOP_ICON_CARD_H);
        draw_string_clipped(surface, label_x, y + DESKTOP_ICON_LABEL_Y + 8, label ? label : "",
                            label_text, x, y, DESKTOP_ICON_CARD_W, DESKTOP_ICON_CARD_H);
    }
}

static void format_rate_short(char* out, uint32_t bytes_per_sec) {
    uint32_t whole;
    uint32_t frac;

    if (!out) return;
    if (bytes_per_sec >= 1024U * 1024U) {
        whole = bytes_per_sec / (1024U * 1024U);
        frac = ((bytes_per_sec % (1024U * 1024U)) * 10U) / (1024U * 1024U);
        if (whole > 99U) whole = 99U;
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

static void draw_taskbar_network_panel(user_gui_surface_t* surface, int x, int y, int w, int h) {
    static uint32_t last_sample_tick = 0;
    static uint32_t last_rx_bytes = 0;
    static uint32_t last_tx_bytes = 0;
    static uint16_t rx_hist[24];
    static uint16_t tx_hist[24];
    net_stats_t stats;
    char rx_buf[6];
    char tx_buf[6];
    uint32_t max_rate = 1U;

    if (!surface) return;
    if (user_net_get_stats(&stats) != 0 || !stats.available) {
        draw_xfce_button_clipped(surface, x, y, w, h, 3, 0x4C5661, UI_TASKBAR_PANEL, 0x20262D, 0, x, y, w, h);
        draw_string_clipped(surface, x + 10, y + 8, "offline", UI_TEXT_MUTED, x, y, w, h);
        return;
    }

    if (last_sample_tick == 0U || user_uptime_ticks() - last_sample_tick >= 100U) {
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
        last_sample_tick = user_uptime_ticks();
    }

    for (int i = 0; i < 24; i++) {
        if (rx_hist[i] > max_rate) max_rate = rx_hist[i];
        if (tx_hist[i] > max_rate) max_rate = tx_hist[i];
    }

    draw_xfce_button_clipped(surface, x, y, w, h, 3, 0x4C5661, UI_TASKBAR_PANEL, 0x20262D, 0, x, y, w, h);
    draw_string_clipped(surface, x + 8, y + 4, stats.configured ? "NET" : "DHCP", UI_TEXT_MUTED, x, y, w, h);
    for (int i = 0; i < 24; i++) {
        int bar_h_rx = (int)((rx_hist[i] * 10U) / max_rate);
        int bar_h_tx = (int)((tx_hist[i] * 10U) / max_rate);
        int px = x + 8 + i * 2;

        if (bar_h_rx > 0) user_gui_fill_rect(surface, px, y + 20 - bar_h_rx, 1, bar_h_rx, UI_ACCENT_ALT);
        if (bar_h_tx > 0) user_gui_fill_rect(surface, px + 1, y + 20 - bar_h_tx, 1, bar_h_tx, UI_SUCCESS);
    }
    format_rate_short(rx_buf, rx_hist[23]);
    format_rate_short(tx_buf, tx_hist[23]);
    draw_string_clipped(surface, x + 58, y + 4, rx_buf, UI_ACCENT_ALT, x, y, w, h);
    draw_string_clipped(surface, x + 58, y + 14, tx_buf, UI_SUCCESS, x, y, w, h);
}

static int load_desktop_icons(desktop_icon_entry_t* entries, int max_entries, int screen_w, int screen_h) {
    int count = 0;
    int desktop_dir = user_fs_find_node("/home/user/Desktop");

    if (!entries || max_entries <= 0) return 0;

    entries[count].kind = DESKTOP_ICON_KIND_THIS_PC;
    entries[count].node_idx = desktop_dir;
    desktop_icon_rect(count, screen_w, screen_h, &entries[count].x, &entries[count].y);
    entries[count].accent = 0x7FB3FF;
    copy_icon_label(entries[count].label, sizeof(entries[count].label), "This PC");
    copy_text(entries[count].path, sizeof(entries[count].path), "/home/user/Desktop");
    count++;

    if (count < max_entries) {
        entries[count].kind = DESKTOP_ICON_KIND_SNAKE;
        entries[count].node_idx = -1;
        desktop_icon_rect(count, screen_w, screen_h, &entries[count].x, &entries[count].y);
        entries[count].accent = 0xF07C82;
        copy_icon_label(entries[count].label, sizeof(entries[count].label), "Snake");
        entries[count].path[0] = '\0';
        count++;
    }

    if (count < max_entries) {
        entries[count].kind = DESKTOP_ICON_KIND_DOOM;
        entries[count].node_idx = -1;
        desktop_icon_rect(count, screen_w, screen_h, &entries[count].x, &entries[count].y);
        entries[count].accent = 0xC58C5A;
        copy_icon_label(entries[count].label, sizeof(entries[count].label), "Doom");
        entries[count].path[0] = '\0';
        count++;
    }

    if (count < max_entries) {
        entries[count].kind = DESKTOP_ICON_KIND_SETTINGS;
        entries[count].node_idx = -1;
        desktop_icon_rect(count, screen_w, screen_h, &entries[count].x, &entries[count].y);
        entries[count].accent = 0x8AD3C3;
        copy_icon_label(entries[count].label, sizeof(entries[count].label), "Settings");
        entries[count].path[0] = '\0';
        count++;
    }

    if (desktop_dir >= 0) {
        for (int idx = 0; idx < MAX_FILES && count < max_entries; idx++) {
            disk_fs_node_t node;

            if (user_fs_get_node_info(idx, &node) != 0) continue;
            if ((int)node.parent_index != desktop_dir || node.flags == 0U) continue;
            entries[count].kind = node.flags == FS_NODE_DIR ? DESKTOP_ICON_KIND_DIR : DESKTOP_ICON_KIND_FILE;
            entries[count].node_idx = idx;
            desktop_icon_rect(count, screen_w, screen_h, &entries[count].x, &entries[count].y);
            entries[count].accent = node.flags == FS_NODE_DIR ? 0xB9E36C : 0xF4C95D;
            copy_icon_label(entries[count].label, sizeof(entries[count].label), node.name);
            if (user_fs_get_path(idx, entries[count].path, (uint32_t)sizeof(entries[count].path)) != 0) {
                entries[count].path[0] = '\0';
            }
            count++;
        }
    }

    return count;
}

static void desktop_icon_cache_invalidate(void) {
    desktop_icon_cache.valid = 0;
}

static const desktop_icon_entry_t* desktop_icon_cache_get(int screen_w, int screen_h, int* out_count) {
    uint32_t now = user_uptime_ticks();

    if (!desktop_icon_cache.valid ||
        desktop_icon_cache.screen_w != screen_w ||
        desktop_icon_cache.screen_h != screen_h ||
        now - desktop_icon_cache.loaded_tick >= DESKTOP_ICON_CACHE_TICKS) {
        desktop_icon_cache.count = load_desktop_icons(desktop_icon_cache.entries, DESKTOP_ICON_MAX, screen_w, screen_h);
        desktop_icon_cache.screen_w = screen_w;
        desktop_icon_cache.screen_h = screen_h;
        desktop_icon_cache.loaded_tick = now;
        desktop_icon_cache.valid = 1;
    }
    if (out_count) *out_count = desktop_icon_cache.count;
    return desktop_icon_cache.entries;
}

static int hit_test_desktop_icon(const desktop_icon_entry_t* entries, int count, int mx, int my) {
    if (!entries) return -1;
    for (int i = 0; i < count; i++) {
        if (point_in_rect(mx, my, entries[i].x, entries[i].y, DESKTOP_ICON_CARD_W, DESKTOP_ICON_CARD_H)) {
            return i;
        }
    }
    return -1;
}

static void activate_desktop_icon(const desktop_icon_entry_t* icon, char* status_text, size_t status_size) {
    const char* argv[2];
    int rc = -1;

    if (!icon) return;
    if (desktop_launch_is_throttled(icon->kind, icon->path)) {
        copy_text(status_text, status_size, "Already opening");
        return;
    }
    switch (icon->kind) {
        case DESKTOP_ICON_KIND_THIS_PC:
            argv[0] = "/bin/explorer";
            argv[1] = icon->path;
            rc = user_spawn("/bin/explorer", argv, 2U);
            copy_text(status_text, status_size, rc == 0 ? "Explorer opened" : "Explorer open failed");
            break;
        case DESKTOP_ICON_KIND_SNAKE:
            argv[0] = "/bin/snake";
            rc = user_spawn("/bin/snake", argv, 1U);
            copy_text(status_text, status_size, rc == 0 ? "Snake opened" : "Snake open failed");
            break;
        case DESKTOP_ICON_KIND_DOOM:
            argv[0] = "/bin/doom";
            rc = user_spawn("/bin/doom", argv, 1U);
            copy_text(status_text, status_size, rc == 0 ? "Doom opened" : "Doom open failed");
            break;
        case DESKTOP_ICON_KIND_SETTINGS:
            argv[0] = "/bin/settings";
            rc = user_spawn("/bin/settings", argv, 1U);
            copy_text(status_text, status_size, rc == 0 ? "Settings opened" : "Settings open failed");
            break;
        case DESKTOP_ICON_KIND_DIR:
            argv[0] = "/bin/explorer";
            argv[1] = icon->path;
            rc = user_spawn("/bin/explorer", argv, 2U);
            copy_text(status_text, status_size, rc == 0 ? "Folder opened" : "Folder open failed");
            break;
        case DESKTOP_ICON_KIND_FILE:
            argv[0] = "/bin/narcpad";
            argv[1] = icon->path;
            rc = user_spawn("/bin/narcpad", argv, 2U);
            copy_text(status_text, status_size, rc == 0 ? "File opened" : "File open failed");
            break;
        default:
            copy_text(status_text, status_size, "Unknown desktop icon");
            break;
    }
}

static void draw_taskbar_legacy(user_gui_surface_t* surface, int width, const gui_window_snapshot_entry_t* windows,
                                int window_count, int hovered_task_slot, int menu_visible,
                                int clip_x, int clip_y, int clip_w, int clip_h) {
    int bar_y;
    int app_x;
    int current_slot_x;
    int clock_x;
    int net_x;
    int settings_x;

    if (!surface) return;
    bar_y = START_BUTTON_Y;
    app_x = START_BUTTON_X + START_BUTTON_W + 10;
    current_slot_x = TASK_SLOT_X;
    clock_x = width - CLOCK_BOX_W - 12;
    net_x = clock_x - TASKBAR_NET_W - 8;
    settings_x = net_x - SETTINGS_BUTTON_W - 8;

    user_gui_fill_rect_alpha(surface, clip_x, clip_y, clip_w,
                             (TASKBAR_HEIGHT + 4 < clip_y + clip_h ? TASKBAR_HEIGHT + 4 : clip_y + clip_h) - clip_y,
                             UI_SHADOW, 72);
    fill_vertical_gradient_clipped(surface, 0, 0, width, TASKBAR_HEIGHT, UI_TASKBAR_BG, UI_TASKBAR_MID,
                                   clip_x, clip_y, clip_w, clip_h);
    user_gui_fill_rect_alpha(surface, clip_x, TASKBAR_HEIGHT - 1, clip_w, 1, UI_TASKBAR_EDGE, 230);
    user_gui_fill_rect_alpha(surface, clip_x, 0, clip_w, 1, UI_HILITE_SOFT, 42);
    draw_xfce_button_clipped(surface, START_BUTTON_X, bar_y, START_BUTTON_W, START_BUTTON_H, 3,
                             menu_visible ? 0xE5EDF4 : UI_TASKBAR_PANEL_ALT,
                             menu_visible ? UI_ACCENT_ALT : UI_TASKBAR_PANEL,
                             menu_visible ? UI_ACCENT_ALT : 0x20262D,
                             menu_visible, clip_x, clip_y, clip_w, clip_h);
    if (rects_intersect(clip_x, clip_y, clip_w, clip_h, START_BUTTON_X, bar_y, START_BUTTON_W, START_BUTTON_H)) {
        user_gui_draw_icon(surface, USER_GUI_ICON_NARCOS, START_BUTTON_X + 11, bar_y + 4, 17,
                           menu_visible ? UI_TEXT_DARK : UI_ACCENT_ALT, menu_visible);
        draw_string_clipped(surface, START_BUTTON_X + 36, bar_y + 9, "NarcOS",
                            menu_visible ? UI_TEXT_DARK : UI_TEXT, clip_x, clip_y, clip_w, clip_h);
        user_gui_fill_rect_alpha(surface, START_BUTTON_X + START_BUTTON_W - 17, bar_y + 8, 1, 10,
                                 menu_visible ? UI_TEXT_DARK : UI_ACCENT_ALT, 180);
    }
    draw_xfce_button_clipped(surface, app_x, bar_y, APP_BUTTON_W, START_BUTTON_H, 3,
                             0x4C5661, UI_TASKBAR_PANEL, 0x20262D, 0,
                             clip_x, clip_y, clip_w, clip_h);
    if (rects_intersect(clip_x, clip_y, clip_w, clip_h, app_x, bar_y, APP_BUTTON_W, START_BUTTON_H)) {
        user_gui_draw_icon(surface, USER_GUI_ICON_TERMINAL, app_x + 10, bar_y + 4, 17, UI_ACCENT_ALT, 0);
        draw_string_clipped(surface, app_x + 32, bar_y + 9, "Terminal", UI_TEXT,
                            clip_x, clip_y, clip_w, clip_h);
    }

    for (int i = 0; i < window_count; i++) {
        char title_buf[10];
        uint32_t top = 0x151D26;
        uint32_t bottom = UI_TASKBAR_PANEL;
        uint32_t border = UI_BORDER_SOFT;
        uint32_t text = UI_TEXT_MUTED;

        if (windows[i].window_id == 0) continue;
        if (current_slot_x + TASK_SLOT_W > net_x - 8) break;
        copy_title_label(title_buf, sizeof(title_buf), windows[i].title);
        if ((windows[i].flags & GUI_WINDOW_SNAPSHOT_ACTIVE) != 0U) {
            top = mix_color(UI_ACCENT_ALT, UI_TASKBAR_PANEL, 84);
            bottom = UI_ACCENT;
            border = UI_ACCENT_ALT;
            text = UI_TEXT_DARK;
        } else if (i == hovered_task_slot) {
            top = mix_color(UI_ACCENT_ALT, UI_TASKBAR_PANEL, 68);
            bottom = UI_TASKBAR_PANEL_ALT;
            border = UI_ACCENT_ALT;
            text = UI_TEXT;
        } else if ((windows[i].flags & GUI_WINDOW_SNAPSHOT_MINIMIZED) != 0U) {
            top = 0x0E141B;
            bottom = 0x111820;
            border = UI_BORDER_SOFT;
            text = UI_TEXT_MUTED;
        }
        draw_xfce_button_clipped(surface, current_slot_x, bar_y, TASK_SLOT_W, START_BUTTON_H, 3,
                                 top, bottom, border,
                                 (windows[i].flags & GUI_WINDOW_SNAPSHOT_ACTIVE) != 0U,
                                 clip_x, clip_y, clip_w, clip_h);
        if (rects_intersect(clip_x, clip_y, clip_w, clip_h, current_slot_x, bar_y, TASK_SLOT_W, START_BUTTON_H)) {
            uint32_t state_color = (windows[i].flags & GUI_WINDOW_SNAPSHOT_ACTIVE) != 0U ? UI_TEXT_DARK :
                                   ((windows[i].flags & GUI_WINDOW_SNAPSHOT_MINIMIZED) != 0U ? UI_TEXT_SUBTLE : UI_ACCENT_ALT);
            int state_alpha = (windows[i].flags & GUI_WINDOW_SNAPSHOT_MINIMIZED) != 0U ? 92 : 220;

            user_gui_draw_rounded_rect(surface, current_slot_x + 8, bar_y + 8, 7, 7, 3,
                                       state_color, state_alpha);
            user_gui_fill_rect_alpha(surface, current_slot_x + 10, bar_y + START_BUTTON_H - 3, TASK_SLOT_W - 20, 2,
                                     (windows[i].flags & GUI_WINDOW_SNAPSHOT_ACTIVE) != 0U ? UI_TEXT_DARK : UI_ACCENT_ALT,
                                     (windows[i].flags & GUI_WINDOW_SNAPSHOT_MINIMIZED) != 0U ? 36 : 120);
            draw_string_clipped(surface, current_slot_x + 23, bar_y + 9, title_buf, text,
                                clip_x, clip_y, clip_w, clip_h);
            if ((windows[i].flags & GUI_WINDOW_SNAPSHOT_MINIMIZED) != 0U) {
                user_gui_fill_rect_alpha(surface, current_slot_x + TASK_SLOT_W - 22, bar_y + 15, 10, 2,
                                         UI_TEXT_SUBTLE, 160);
            }
        }
        current_slot_x += TASK_SLOT_W + TASK_SLOT_GAP;
    }

    draw_xfce_button_clipped(surface, settings_x, bar_y, SETTINGS_BUTTON_W, START_BUTTON_H, 3,
                             0x4C5661, UI_TASKBAR_PANEL, 0x20262D, 0,
                             clip_x, clip_y, clip_w, clip_h);
    if (rects_intersect(clip_x, clip_y, clip_w, clip_h, settings_x, bar_y, SETTINGS_BUTTON_W, START_BUTTON_H)) {
        user_gui_draw_icon(surface, USER_GUI_ICON_SETTINGS, settings_x + 10, bar_y + 4, 17, UI_ACCENT_ALT, 0);
        draw_string_clipped(surface, settings_x + 33, bar_y + 9, "Settings", UI_TEXT,
                            clip_x, clip_y, clip_w, clip_h);
    }
    if (rects_intersect(clip_x, clip_y, clip_w, clip_h, net_x, bar_y, TASKBAR_NET_W, START_BUTTON_H)) {
        draw_taskbar_network_panel(surface, net_x, bar_y, TASKBAR_NET_W, START_BUTTON_H);
    }
}

static void copy_title_label(char* dst, size_t dst_size, const char* src) {
    size_t i = 0;

    if (!dst || dst_size == 0U) return;
    if (!src) src = "";
    while (src[i] != '\0' && i + 1U < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void launch_menu_action(int item_idx, char* status_text, size_t status_size) {
    static const char* explorer_argv[] = {"/bin/explorer", "/home/user/Desktop"};
    static const char* settings_argv[] = {"/bin/settings"};
    static const char* snake_argv[] = {"/bin/snake"};
    static const char* doom_argv[] = {"/bin/doom"};
    static const char* narcpad_readme_argv[] = {"/bin/narcpad", "/home/user/Desktop/readme.txt"};
    static const char* hello_argv[] = {"/bin/hello"};
    static const char* neofetch_argv[] = {"/bin/neofetch"};
    int rc = -1;

    if (desktop_launch_is_throttled(1000 + item_idx, "")) {
        copy_text(status_text, status_size, "Already opening");
        return;
    }
    switch (item_idx) {
        case 0:
            rc = user_spawn("/bin/explorer", explorer_argv, 2U);
            copy_text(status_text, status_size, rc == 0 ? "Explorer opened" : "Explorer open failed");
            break;
        case 1:
            rc = user_spawn("/bin/settings", settings_argv, 1U);
            copy_text(status_text, status_size, rc == 0 ? "Settings opened" : "Settings open failed");
            break;
        case 2:
            rc = user_spawn("/bin/snake", snake_argv, 1U);
            copy_text(status_text, status_size, rc == 0 ? "Snake opened" : "Snake open failed");
            break;
        case 3:
            rc = user_spawn("/bin/doom", doom_argv, 1U);
            copy_text(status_text, status_size, rc == 0 ? "Doom opened" : "Doom open failed");
            break;
        case 4:
            rc = user_spawn("/bin/narcpad", narcpad_readme_argv, 2U);
            copy_text(status_text, status_size, rc == 0 ? "Readme opened in NarcPad" : "Readme open failed");
            break;
        case 5:
            rc = user_spawn("/bin/hello", hello_argv, 1U);
            copy_text(status_text, status_size, rc >= 0 ? "Spawned /bin/hello" : "Spawn /bin/hello failed");
            break;
        case 6:
            rc = user_spawn("/bin/neofetch", neofetch_argv, 1U);
            copy_text(status_text, status_size, rc >= 0 ? "Spawned /bin/neofetch" : "Spawn /bin/neofetch failed");
            break;
        default:
            copy_text(status_text, status_size, "Unknown menu action");
            break;
    }
}

static int desktop_launch_is_throttled(int key, const char* path) {
    uint32_t now = user_uptime_ticks();

    if (!path) path = "";
    if (desktop_last_launch_key == key &&
        userlib_strcmp(desktop_last_launch_path, path) == 0 &&
        now - desktop_last_launch_tick < DESKTOP_LAUNCH_THROTTLE_TICKS) {
        return 1;
    }
    desktop_last_launch_key = key;
    desktop_last_launch_tick = now;
    copy_text(desktop_last_launch_path, sizeof(desktop_last_launch_path), path);
    return 0;
}

static int desktop_build_unique_path(char* out, size_t out_size, const char* base, const char* ext) {
    if (!out || out_size == 0U || !base || !ext) return -1;
    for (uint32_t n = 1U; n < 100U; n++) {
        out[0] = '\0';
        if (append_text(out, out_size, "/home/user/Desktop/") != 0) return -1;
        if (append_text(out, out_size, base) != 0) return -1;
        if (n > 1U && append_uint(out, out_size, n) != 0) return -1;
        if (append_text(out, out_size, ext) != 0) return -1;
        if (user_fs_find_node(out) < 0) return 0;
    }
    return -1;
}

static void create_desktop_file(char* status_text, size_t status_size) {
    char path[256];
    int rc;

    if (desktop_build_unique_path(path, sizeof(path), "NewFile", ".txt") != 0) {
        copy_text(status_text, status_size, "New file name failed");
        return;
    }
    rc = user_fs_touch(path);
    if (rc == 0) desktop_icon_cache_invalidate();
    copy_text(status_text, status_size, rc == 0 ? "New file created" : "New file failed");
}

static void create_desktop_folder(char* status_text, size_t status_size) {
    char path[256];
    int rc;

    if (desktop_build_unique_path(path, sizeof(path), "NewFolder", "") != 0) {
        copy_text(status_text, status_size, "New folder name failed");
        return;
    }
    rc = user_fs_mkdir(path);
    if (rc == 0) desktop_icon_cache_invalidate();
    copy_text(status_text, status_size, rc == 0 ? "New folder created" : "New folder failed");
}

static void launch_context_action(int item_idx, char* status_text, size_t status_size) {
    switch (item_idx) {
        case 0:
            create_desktop_file(status_text, status_size);
            break;
        case 1:
            create_desktop_folder(status_text, status_size);
            break;
        case 2:
            desktop_icon_cache_invalidate();
            copy_text(status_text, status_size, "Desktop refreshed");
            break;
        case 3:
            launch_menu_action(1, status_text, status_size);
            break;
        default: copy_text(status_text, status_size, "Unknown context action"); break;
    }
}

static void launch_terminal_action(char* status_text, size_t status_size) {
    gui_desktop_window_action_t action;
    int rc;

    action.size = sizeof(action);
    action.window_id = 0;
    action.action = GUI_DESKTOP_WINDOW_FOCUS;
    action.x = 0;
    action.y = 0;
    action.width = 0;
    action.height = 0;
    action.event_type = 0;
    action.event_arg0 = 0;
    action.event_arg1 = 0;
    action.event_arg2 = 0;
    rc = user_gui_desktop_window_action(&action);
    copy_text(status_text, status_size, rc == 0 ? "Terminal focused" : "Terminal focus failed");
}

static void refresh_window_list(gui_window_snapshot_entry_t* entries, int* out_count) {
    int count;

    if (!entries || !out_count) return;
    count = user_gui_list_windows(entries, TASK_SLOT_MAX);
    if (count < 0) count = 0;
    *out_count = count;
}

static int hit_test_resize(const gui_window_snapshot_entry_t* win, int mx, int my) {
    int flags = 0;

    if (!win) return 0;
    if ((win->flags & GUI_WINDOW_SNAPSHOT_BORDERLESS) != 0U ||
        (win->flags & GUI_WINDOW_SNAPSHOT_FULLSCREEN) != 0U) {
        return 0;
    }
    if (!point_in_rect(mx, my, win->x, win->y, win->width, win->height)) return 0;
    if (mx <= win->x + WINDOW_EDGE) flags |= RESIZE_LEFT;
    if (mx >= win->x + win->width - WINDOW_EDGE) flags |= RESIZE_RIGHT;
    if (my >= win->y + win->height - WINDOW_EDGE) flags |= RESIZE_BOTTOM;
    return flags;
}

static int hit_test_window_control(const gui_window_snapshot_entry_t* win, int mx, int my) {
    int close_x;
    int max_x;
    int min_x;

    if (!win) return WINDOW_CTRL_NONE;
    if ((win->flags & GUI_WINDOW_SNAPSHOT_BORDERLESS) != 0U ||
        (win->flags & GUI_WINDOW_SNAPSHOT_FULLSCREEN) != 0U ||
        (win->flags & GUI_WINDOW_SNAPSHOT_MINIMIZED) != 0U) {
        return WINDOW_CTRL_NONE;
    }
    if (!point_in_rect(mx, my, win->x, win->y, win->width, WINDOW_CAPTION_H)) return WINDOW_CTRL_NONE;
    close_x = win->x + win->width - WINDOW_CTRL_RIGHT_PAD - WINDOW_CTRL_SIZE;
    max_x = close_x - WINDOW_CTRL_GAP - WINDOW_CTRL_SIZE;
    min_x = max_x - WINDOW_CTRL_GAP - WINDOW_CTRL_SIZE;
    if (point_in_rect(mx, my, close_x, win->y + WINDOW_CTRL_TOP, WINDOW_CTRL_SIZE, WINDOW_CTRL_SIZE)) {
        return WINDOW_CTRL_CLOSE;
    }
    if (point_in_rect(mx, my, max_x, win->y + WINDOW_CTRL_TOP, WINDOW_CTRL_SIZE, WINDOW_CTRL_SIZE)) {
        return WINDOW_CTRL_MAXIMIZE;
    }
    if (point_in_rect(mx, my, min_x, win->y + WINDOW_CTRL_TOP, WINDOW_CTRL_SIZE, WINDOW_CTRL_SIZE)) {
        return WINDOW_CTRL_MINIMIZE;
    }
    return WINDOW_CTRL_NONE;
}

static int hit_test_window(const gui_window_snapshot_entry_t* windows, int window_count, int mx, int my) {
    for (int i = window_count - 1; i >= 0; i--) {
        if ((windows[i].flags & GUI_WINDOW_SNAPSHOT_MINIMIZED) != 0U) continue;
        if (point_in_rect(mx, my, windows[i].x, windows[i].y, windows[i].width, windows[i].height)) return i;
    }
    return -1;
}

static int find_task_slot(const gui_window_snapshot_entry_t* windows, int window_count, int width, int mx, int my) {
    int slot_idx = 0;
    for (int i = 0; i < window_count; i++) {
        int x;

        if (windows[i].window_id == 0) continue;
        x = TASK_SLOT_X + slot_idx * (TASK_SLOT_W + TASK_SLOT_GAP);
        if (x + TASK_SLOT_W > width - (CLOCK_BOX_W + TASKBAR_NET_W + SETTINGS_BUTTON_W + 44)) break;
        if (point_in_rect(mx, my, x, TASK_SLOT_Y, TASK_SLOT_W, TASK_SLOT_H)) return i;
        slot_idx++;
    }
    return -1;
}

static int find_active_window(const gui_window_snapshot_entry_t* windows, int window_count) {
    for (int i = 0; i < window_count; i++) {
        if ((windows[i].flags & GUI_WINDOW_SNAPSHOT_ACTIVE) != 0U) return i;
    }
    return -1;
}

static int window_accepts_client_input(const gui_window_snapshot_entry_t* win) {
    return win && (win->flags & GUI_WINDOW_SNAPSHOT_USER) != 0U;
}

static int find_window_index_by_id(const gui_window_snapshot_entry_t* windows, int window_count, int window_id) {
    for (int i = 0; i < window_count; i++) {
        if (windows[i].window_id == window_id) return i;
    }
    return -1;
}

static void mark_all_window_caches_dirty(int* cache_dirty_flags, int cache_count) {
    if (!cache_dirty_flags || cache_count <= 0) return;
    for (int i = 0; i < cache_count; i++) cache_dirty_flags[i] = 1;
}

static void mark_window_cache_dirty(const gui_window_snapshot_entry_t* windows, int window_count,
                                    int* cache_dirty_flags, int cache_count, int window_id) {
    int idx;

    if (!cache_dirty_flags || cache_count <= 0 || window_id < 0) return;
    idx = find_window_index_by_id(windows, window_count, window_id);
    if (idx < 0 || idx >= cache_count) return;
    cache_dirty_flags[idx] = 1;
}

static int resolve_input_window(const gui_window_snapshot_entry_t* windows, int window_count, int preferred_window_id) {
    int preferred_idx = find_window_index_by_id(windows, window_count, preferred_window_id);
    if (preferred_idx != -1 && (windows[preferred_idx].flags & GUI_WINDOW_SNAPSHOT_MINIMIZED) == 0U) return preferred_idx;
    return find_active_window(windows, window_count);
}

static void update_window_snapshot_rect(gui_window_snapshot_entry_t* windows, int window_count,
                                        int window_id, int x, int y, int w, int h) {
    int idx = find_window_index_by_id(windows, window_count, window_id);

    if (idx < 0) return;
    windows[idx].x = x;
    windows[idx].y = y;
    windows[idx].width = w;
    windows[idx].height = h;
}

static int submit_window_rect(int window_id, int x, int y, int w, int h) {
    gui_desktop_window_action_t action;

    action.size = sizeof(action);
    action.window_id = window_id;
    action.action = GUI_DESKTOP_WINDOW_SET_RECT;
    action.x = x;
    action.y = y;
    action.width = w;
    action.height = h;
    action.event_type = 0;
    action.event_arg0 = 0;
    action.event_arg1 = 0;
    action.event_arg2 = 0;
    return user_gui_desktop_window_action(&action);
}

static int find_layout_state(window_layout_state_t* states, int state_count, int window_id, int create) {
    int free_idx = -1;

    if (!states || state_count <= 0) return -1;
    for (int i = 0; i < state_count; i++) {
        if (states[i].window_id == window_id) return i;
        if (free_idx == -1 && states[i].window_id < 0) free_idx = i;
    }
    if (!create || free_idx == -1) return -1;
    states[free_idx].window_id = window_id;
    states[free_idx].valid = 0;
    states[free_idx].mode = WINDOW_LAYOUT_NONE;
    states[free_idx].x = 0;
    states[free_idx].y = 0;
    states[free_idx].w = 0;
    states[free_idx].h = 0;
    return free_idx;
}

static void forget_layout_state(window_layout_state_t* states, int state_count, int window_id) {
    int idx = find_layout_state(states, state_count, window_id, 0);

    if (idx == -1) return;
    states[idx].window_id = -1;
    states[idx].valid = 0;
    states[idx].mode = WINDOW_LAYOUT_NONE;
}

static void compute_layout_rect(int mode, int screen_w, int screen_h, int* out_x, int* out_y, int* out_w, int* out_h) {
    int work_x = 8;
    int work_y = TASKBAR_HEIGHT + 8;
    int work_w = screen_w - 16;
    int work_h = screen_h - TASKBAR_HEIGHT - 16;

    if (work_w < 160) work_w = screen_w;
    if (work_h < 120) work_h = screen_h - TASKBAR_HEIGHT;
    if (work_h < 100) work_h = 100;
    if (mode == WINDOW_LAYOUT_SNAP_LEFT) {
        work_w = (work_w - 6) / 2;
    } else if (mode == WINDOW_LAYOUT_SNAP_RIGHT) {
        int half_w = (work_w - 6) / 2;

        work_x = screen_w - 8 - half_w;
        work_w = half_w;
    }
    if (out_x) *out_x = work_x;
    if (out_y) *out_y = work_y;
    if (out_w) *out_w = work_w;
    if (out_h) *out_h = work_h;
}

static int apply_window_layout(window_layout_state_t* states, int state_count,
                               gui_window_snapshot_entry_t* windows, int window_count,
                               const gui_window_snapshot_entry_t* win, int mode,
                               int screen_w, int screen_h,
                               int restore_x, int restore_y, int restore_w, int restore_h) {
    int idx;
    int layout_x;
    int layout_y;
    int layout_w;
    int layout_h;

    if (!win || mode == WINDOW_LAYOUT_NONE) return -1;
    idx = find_layout_state(states, state_count, win->window_id, 1);
    if (idx == -1) return -1;
    if (!states[idx].valid) {
        states[idx].x = restore_x;
        states[idx].y = restore_y;
        states[idx].w = restore_w;
        states[idx].h = restore_h;
        states[idx].valid = 1;
    }
    states[idx].mode = mode;
    compute_layout_rect(mode, screen_w, screen_h, &layout_x, &layout_y, &layout_w, &layout_h);
    if (submit_window_rect(win->window_id, layout_x, layout_y, layout_w, layout_h) != 0) return -1;
    update_window_snapshot_rect(windows, window_count, win->window_id, layout_x, layout_y, layout_w, layout_h);
    return 0;
}

static int toggle_window_maximize(window_layout_state_t* states, int state_count,
                                  gui_window_snapshot_entry_t* windows, int window_count,
                                  const gui_window_snapshot_entry_t* win,
                                  int screen_w, int screen_h) {
    int idx;

    if (!win) return -1;
    idx = find_layout_state(states, state_count, win->window_id, 0);
    if (idx != -1 && states[idx].valid && states[idx].mode == WINDOW_LAYOUT_MAXIMIZED) {
        int restore_x = states[idx].x;
        int restore_y = states[idx].y;
        int restore_w = states[idx].w;
        int restore_h = states[idx].h;

        forget_layout_state(states, state_count, win->window_id);
        if (submit_window_rect(win->window_id, restore_x, restore_y, restore_w, restore_h) != 0) return -1;
        update_window_snapshot_rect(windows, window_count, win->window_id, restore_x, restore_y, restore_w, restore_h);
        return 1;
    }
    if (apply_window_layout(states, state_count, windows, window_count, win, WINDOW_LAYOUT_MAXIMIZED,
                            screen_w, screen_h, win->x, win->y, win->width, win->height) != 0) {
        return -1;
    }
    return 0;
}

static void clamp_drag_restore_rect(int* x, int* y, int w, int h, int screen_w, int screen_h) {
    int min_x;
    int max_x;
    int min_y = TASKBAR_HEIGHT + 4;
    int max_y;

    if (!x || !y || w <= 0 || h <= 0) return;
    min_x = 8 - w + WINDOW_DRAG_VISIBLE_EDGE;
    max_x = screen_w - WINDOW_DRAG_VISIBLE_EDGE;
    max_y = screen_h - WINDOW_DRAG_VISIBLE_EDGE;
    if (max_x < 8) max_x = 8;
    if (max_y < min_y) max_y = min_y;
    if (*x < min_x) *x = min_x;
    if (*x > max_x) *x = max_x;
    if (*y < min_y) *y = min_y;
    if (*y > max_y) *y = max_y;
}

static int restore_layout_before_drag(window_layout_state_t* states, int state_count,
                                      gui_window_snapshot_entry_t* windows, int window_count,
                                      int window_id, int mouse_x, int mouse_y,
                                      int screen_w, int screen_h) {
    int state_idx;
    int restore_x;
    int restore_y;
    int restore_w;
    int restore_h;

    state_idx = find_layout_state(states, state_count, window_id, 0);
    if (state_idx == -1 ||
        !states[state_idx].valid ||
        states[state_idx].mode == WINDOW_LAYOUT_NONE) {
        return 0;
    }

    restore_w = states[state_idx].w;
    restore_h = states[state_idx].h;
    restore_x = mouse_x - restore_w / 2;
    restore_y = mouse_y - WINDOW_CAPTION_H / 2;
    clamp_drag_restore_rect(&restore_x, &restore_y, restore_w, restore_h, screen_w, screen_h);
    if (submit_window_rect(window_id, restore_x, restore_y, restore_w, restore_h) == 0) {
        forget_layout_state(states, state_count, window_id);
        update_window_snapshot_rect(windows, window_count, window_id, restore_x, restore_y, restore_w, restore_h);
        return 1;
    }
    return 0;
}

static void submit_window_command(int window_id, uint32_t command) {
    gui_desktop_window_action_t action;

    action.size = sizeof(action);
    action.window_id = window_id;
    action.action = command;
    action.x = 0;
    action.y = 0;
    action.width = 0;
    action.height = 0;
    action.event_type = 0;
    action.event_arg0 = 0;
    action.event_arg1 = 0;
    action.event_arg2 = 0;
    (void)user_gui_desktop_window_action(&action);
}

static int submit_window_input(int window_id, uint32_t event_type, int arg0, int arg1, int arg2) {
    gui_desktop_window_action_t action;

    action.size = sizeof(action);
    action.window_id = window_id;
    action.action = GUI_DESKTOP_WINDOW_DELIVER_INPUT;
    action.x = 0;
    action.y = 0;
    action.width = 0;
    action.height = 0;
    action.event_type = event_type;
    action.event_arg0 = arg0;
    action.event_arg1 = arg1;
    action.event_arg2 = arg2;
    return user_gui_desktop_window_action(&action);
}

static int read_window_surface(int window_id, uint32_t** inout_pixels, uint32_t* inout_capacity_pixels,
                               int* out_width, int* out_height, uint32_t* out_flags) {
    uint32_t final_flags = 0U;
    int old_width = out_width ? *out_width : 0;
    int old_height = out_height ? *out_height : 0;
    int force_full_copy = 0;
    int iterations = 0;

    if (!inout_pixels || !inout_capacity_pixels || !out_width || !out_height || !out_flags) return -1;
    do {
        gui_window_surface_read_t request;
        uint32_t surface_width;
        uint32_t surface_height;
        uint32_t needed_pixels;
        uint32_t copy_offset_pixels;
        uint32_t copy_available_pixels;
        int need_full_copy;
        int partial_copy;

        request.size = sizeof(request);
        request.window_id = window_id;
        request.x = 0;
        request.y = 0;
        request.surface_width = 0U;
        request.surface_height = 0U;
        request.buffer_ptr = 0U;
        request.buffer_bytes = 0U;
        request.width = 0U;
        request.height = 0U;
        request.stride_bytes = 0U;
        request.format = 0U;
        request.flags = 0U;
        if (user_gui_read_window_surface(&request) != 0) return -1;
        surface_width = request.surface_width != 0U ? request.surface_width : request.width;
        surface_height = request.surface_height != 0U ? request.surface_height : request.height;
        needed_pixels = surface_width * surface_height;
        if (surface_width == 0U || surface_height == 0U || request.format != GUI_PIXEL_FORMAT_XRGB8888) return -1;
        need_full_copy = force_full_copy ||
                         (*inout_pixels == 0) || (*inout_capacity_pixels < needed_pixels) ||
                         old_width != (int)surface_width || old_height != (int)surface_height;
        if (*inout_pixels == 0 || *inout_capacity_pixels < needed_pixels) {
            uint32_t* new_pixels;

            if (*inout_pixels) user_free(*inout_pixels);
            new_pixels = (uint32_t*)user_malloc((size_t)needed_pixels * sizeof(uint32_t));
            if (!new_pixels) {
                *inout_pixels = 0;
                *inout_capacity_pixels = 0;
                return -1;
            }
            *inout_pixels = new_pixels;
            *inout_capacity_pixels = needed_pixels;
            need_full_copy = 1;
        }
        partial_copy = !need_full_copy &&
                       (request.flags & GUI_WINDOW_SURFACE_FLAG_DIRTY_RECT) != 0U &&
                       request.x >= 0 && request.y >= 0 &&
                       request.width > 0U && request.height > 0U &&
                       (uint32_t)request.x < surface_width &&
                       (uint32_t)request.y < surface_height &&
                       request.width <= surface_width - (uint32_t)request.x &&
                       request.height <= surface_height - (uint32_t)request.y;
        if (partial_copy) {
            copy_offset_pixels = (uint32_t)request.y * surface_width + (uint32_t)request.x;
            if (copy_offset_pixels >= *inout_capacity_pixels) return -1;
            copy_available_pixels = *inout_capacity_pixels - copy_offset_pixels;
            request.buffer_ptr = (uintptr_t)(*inout_pixels + copy_offset_pixels);
            request.buffer_bytes = copy_available_pixels * sizeof(uint32_t);
            request.stride_bytes = surface_width * sizeof(uint32_t);
        } else {
            request.x = 0;
            request.y = 0;
            request.width = surface_width;
            request.height = surface_height;
            request.buffer_ptr = (uintptr_t)*inout_pixels;
            request.buffer_bytes = needed_pixels * sizeof(uint32_t);
            request.stride_bytes = surface_width * sizeof(uint32_t);
        }
        if (user_gui_read_window_surface(&request) != 0) return -1;
        *out_width = (int)surface_width;
        *out_height = (int)surface_height;
        final_flags = request.flags;
        old_width = *out_width;
        old_height = *out_height;
        force_full_copy = 0;
        iterations++;
        if (!partial_copy) break;
        if ((request.flags & GUI_WINDOW_SURFACE_FLAG_MORE_DIRTY_RECTS) == 0U) break;
    } while (iterations < 8);
    *out_flags = final_flags;
    return 0;
}

static void release_window_surface_cache(window_surface_cache_t* cache) {
    if (!cache) return;
    if (cache->pixels) user_free(cache->pixels);
    cache->window_id = -1;
    cache->pixels = 0;
    cache->capacity_pixels = 0;
    cache->width = 0;
    cache->height = 0;
    cache->flags = 0U;
}

static void sync_window_surface_caches(const gui_window_snapshot_entry_t* windows, int window_count,
                                       window_surface_cache_t* caches, int cache_count,
                                       int* cache_dirty_flags, int force_all) {
    for (int i = 0; i < cache_count; i++) {
        if (i >= window_count || (windows[i].flags & GUI_WINDOW_SNAPSHOT_MINIMIZED) != 0U) {
            release_window_surface_cache(&caches[i]);
            if (cache_dirty_flags) cache_dirty_flags[i] = 0;
            continue;
        }
        if (caches[i].window_id != windows[i].window_id) {
            release_window_surface_cache(&caches[i]);
            caches[i].window_id = windows[i].window_id;
            if (cache_dirty_flags) cache_dirty_flags[i] = 1;
        }
        if (!force_all && cache_dirty_flags && cache_dirty_flags[i] == 0) continue;
        if (read_window_surface(windows[i].window_id, &caches[i].pixels, &caches[i].capacity_pixels,
                                &caches[i].width, &caches[i].height, &caches[i].flags) != 0) {
            caches[i].width = 0;
            caches[i].height = 0;
            caches[i].flags = 0U;
        }
        if (cache_dirty_flags) cache_dirty_flags[i] = 0;
    }
}

static int forward_pointer_event(const gui_window_snapshot_entry_t* win, uint32_t event_type,
                                 int mouse_x, int mouse_y, int arg2) {
    int local_x;
    int local_y;
    int client_w;
    int client_h;

    if (!win) return -1;
    if (!window_accepts_client_input(win)) {
        return submit_window_input(win->window_id, event_type, mouse_x, mouse_y, arg2);
    }
    local_x = mouse_x - win->x;
    local_y = mouse_y - win->y;
    if ((win->flags & GUI_WINDOW_SNAPSHOT_BORDERLESS) == 0U &&
        (win->flags & GUI_WINDOW_SNAPSHOT_FULLSCREEN) == 0U) {
        local_x -= WINDOW_CLIENT_INSET_X;
        local_y -= WINDOW_CLIENT_TOP;
        client_w = win->width - WINDOW_CLIENT_INSET_X * 2;
        client_h = win->height - WINDOW_CLIENT_TOP - WINDOW_CLIENT_BOTTOM;
    } else {
        client_w = win->width;
        client_h = win->height;
    }
    if (local_x < 0 || local_y < 0 || local_x >= client_w || local_y >= client_h) return -1;
    return submit_window_input(win->window_id, event_type, local_x, local_y, arg2);
}

static void render_frame(uint32_t* framebuffer, int pitch_pixels, int width, int height,
                         int dirty_x, int dirty_y, int dirty_w, int dirty_h,
                         int focused, int mouse_x, int mouse_y, int click_count,
                         int menu_visible, int hovered_item, int hovered_task_slot,
                         int context_visible, int context_x, int context_y, int hovered_context_item,
                         int drag_window_id, int resize_window_id, int hovered_desktop_icon,
                         const char* status_text, const window_surface_cache_t* surface_caches, int surface_cache_count,
                         const gui_window_snapshot_entry_t* windows, int window_count) {
    user_gui_surface_t surface;
    char clock_text[12];
    const desktop_icon_entry_t* desktop_icons = 0;
    int desktop_icon_count;

    if (!framebuffer || pitch_pixels < width || width < 1 || height < 1) return;
    (void)focused;
    (void)mouse_x;
    (void)mouse_y;
    (void)click_count;
    (void)drag_window_id;
    (void)resize_window_id;
    if (dirty_x < 0) dirty_x = 0;
    if (dirty_y < 0) dirty_y = 0;
    if (dirty_x + dirty_w > width) dirty_w = width - dirty_x;
    if (dirty_y + dirty_h > height) dirty_h = height - dirty_y;
    if (dirty_w <= 0 || dirty_h <= 0) return;
    surface.pixels = framebuffer;
    surface.width = width;
    surface.height = height;
    desktop_icon_count = 0;
    (void)pitch_pixels;
    draw_desktop_background_region(&surface, dirty_x, dirty_y, dirty_w, dirty_h);
    user_gui_fill_rect_alpha(&surface, dirty_x, dirty_y, dirty_w, dirty_h, 0x081018, 22);
    if (rects_intersect(dirty_x, dirty_y, dirty_w, dirty_h,
                        DESKTOP_ICON_AREA_X - 8, DESKTOP_ICON_AREA_Y - 8,
                        width - (DESKTOP_ICON_AREA_X * 2) + 16, height - DESKTOP_ICON_AREA_Y - 64)) {
        desktop_icons = desktop_icon_cache_get(width, height, &desktop_icon_count);
        for (int i = 0; i < desktop_icon_count; i++) {
            if (!rects_intersect(dirty_x, dirty_y, dirty_w, dirty_h,
                                 desktop_icons[i].x, desktop_icons[i].y,
                                 DESKTOP_ICON_CARD_W, DESKTOP_ICON_CARD_H)) {
                continue;
            }
            draw_desktop_icon(&surface, desktop_icons[i].kind, desktop_icons[i].x, desktop_icons[i].y,
                              desktop_icons[i].label, desktop_icons[i].accent, i == hovered_desktop_icon);
        }
    }
    for (int i = 0; i < window_count && i < surface_cache_count; i++) {
        if (!rects_intersect(dirty_x, dirty_y, dirty_w, dirty_h,
                             windows[i].x, windows[i].y, windows[i].width, windows[i].height)) {
            continue;
        }
        draw_window_surface_card(&surface, &windows[i], &surface_caches[i],
                                 dirty_x, dirty_y, dirty_w, dirty_h);
    }
    if (rects_intersect(dirty_x, dirty_y, dirty_w, dirty_h, 0, 0, width, TASKBAR_HEIGHT + 6)) {
        draw_taskbar_legacy(&surface, width, windows, window_count, hovered_task_slot, menu_visible,
                            dirty_x, dirty_y, dirty_w, dirty_h);
    }
    if (rects_intersect(dirty_x, dirty_y, dirty_w, dirty_h,
                        width - CLOCK_BOX_W - 12, START_BUTTON_Y, CLOCK_BOX_W, CLOCK_BOX_H)) {
        format_clock_text(clock_text, sizeof(clock_text));
        draw_xfce_button_clipped(&surface, width - CLOCK_BOX_W - 12, START_BUTTON_Y, CLOCK_BOX_W, CLOCK_BOX_H, 3,
                                 0x4C5661, UI_TASKBAR_PANEL, 0x20262D, 0,
                                 dirty_x, dirty_y, dirty_w, dirty_h);
        draw_string_clipped(&surface, width - CLOCK_BOX_W - 12 + 14, START_BUTTON_Y + 8, clock_text, UI_TEXT,
                            dirty_x, dirty_y, dirty_w, dirty_h);
    }
    if (status_text && status_text[0] != '\0' &&
        rects_intersect(dirty_x, dirty_y, dirty_w, dirty_h, width - 264, height - 40, 246, 28)) {
        draw_panel_elevated_clipped(&surface, width - 264, height - 40, 246, 28, UI_RADIUS_SM,
                                    0x111921, 0x0D1319, UI_BORDER_SOFT, 0U,
                                    dirty_x, dirty_y, dirty_w, dirty_h);
        draw_string_clipped(&surface, width - 252, height - 31, status_text, UI_TEXT_MUTED,
                            dirty_x, dirty_y, dirty_w, dirty_h);
    }

    if (menu_visible &&
        rects_intersect(dirty_x, dirty_y, dirty_w, dirty_h, MENU_X, MENU_Y, MENU_W, start_menu_height())) {
        int menu_h = start_menu_height();

        draw_panel_elevated_clipped(&surface, MENU_X, MENU_Y, MENU_W, menu_h, UI_RADIUS_MD,
                                    0x17212B, 0x111820, UI_BORDER_SOFT, UI_ACCENT_ALT,
                                    dirty_x, dirty_y, dirty_w, dirty_h);
        if (rects_intersect(dirty_x, dirty_y, dirty_w, dirty_h, MENU_X, MENU_Y, MENU_W, MENU_HEADER_H)) {
            user_gui_draw_icon(&surface, USER_GUI_ICON_NARCOS, MENU_X + 16, MENU_Y + 11, 18, UI_ACCENT_ALT, 1);
        }
        draw_tall_string_clipped(&surface, MENU_X + 44, MENU_Y + 10, "NarcOS", UI_TEXT, UI_SHADOW,
                                 dirty_x, dirty_y, dirty_w, dirty_h);
        draw_string_clipped(&surface, MENU_X + MENU_W - 92, MENU_Y + 14, "Start Menu", UI_TEXT_SUBTLE,
                            dirty_x, dirty_y, dirty_w, dirty_h);
        user_gui_fill_rect_alpha(&surface, MENU_X + 14, MENU_Y + MENU_HEADER_H - 2, MENU_W - 28, 1,
                                 UI_BORDER_SOFT, 255);
        draw_string_clipped(&surface, MENU_X + 18, MENU_Y + MENU_HEADER_H + 4, "Pinned",
                            UI_TEXT_SUBTLE, dirty_x, dirty_y, dirty_w, dirty_h);
        draw_string_clipped(&surface, MENU_X + 18,
                            start_menu_item_y(MENU_PRIMARY_ITEMS) - MENU_SECTION_H + 4,
                            "Tools", UI_TEXT_SUBTLE,
                            dirty_x, dirty_y, dirty_w, dirty_h);
        for (int i = 0; i < MENU_ITEMS; i++) {
            int item_y = start_menu_item_y(i);
            uint32_t fill_top = i == hovered_item ? mix_color(UI_ACCENT_ALT, UI_SURFACE_2, 120) : 0x16202A;
            uint32_t fill_bottom = i == hovered_item ? UI_ACCENT : 0x10161D;
            uint32_t border = i == hovered_item ? UI_ACCENT_ALT : UI_BORDER_SOFT;
            uint32_t title = i == hovered_item ? UI_TEXT_DARK : UI_TEXT;
            uint32_t subtitle = i == hovered_item ? UI_TEXT_DARK : UI_TEXT_MUTED;

            draw_panel_elevated_clipped(&surface, MENU_X + 10, item_y, MENU_W - 20, MENU_ITEM_H - 6, UI_RADIUS_SM,
                                        fill_top, fill_bottom, border, i == hovered_item ? UI_ACCENT_ALT : 0U,
                                        dirty_x, dirty_y, dirty_w, dirty_h);
            user_gui_draw_icon(&surface, menu_item_icons[i], MENU_X + 18, item_y + 5, 22,
                               i == hovered_item ? UI_TEXT_DARK : UI_ACCENT_ALT, i == hovered_item);
            draw_tall_string_clipped(&surface, MENU_X + 48, item_y + 4, menu_items[i].label, title, UI_SHADOW,
                                     dirty_x, dirty_y, dirty_w, dirty_h);
            draw_string_clipped(&surface, MENU_X + 48, item_y + 22, menu_items[i].subtitle, subtitle,
                                dirty_x, dirty_y, dirty_w, dirty_h);
        }
    }

    if (context_visible &&
        rects_intersect(dirty_x, dirty_y, dirty_w, dirty_h,
                        context_x, context_y, CTX_MENU_W, 12 + CTX_MENU_ITEMS * CTX_MENU_ITEM_H + 10)) {
        int menu_h = 12 + CTX_MENU_ITEMS * CTX_MENU_ITEM_H + 10;

        draw_panel_elevated_clipped(&surface, context_x, context_y, CTX_MENU_W, menu_h, UI_RADIUS_MD,
                                    0x16202A, 0x10161D, UI_BORDER_SOFT, UI_ACCENT_ALT,
                                    dirty_x, dirty_y, dirty_w, dirty_h);
        for (int i = 0; i < CTX_MENU_ITEMS; i++) {
            int item_y = context_y + 8 + i * CTX_MENU_ITEM_H;
            uint32_t fill_top = i == hovered_context_item ? mix_color(UI_ACCENT_ALT, UI_SURFACE_2, 120) : 0x16202A;
            uint32_t fill_bottom = i == hovered_context_item ? UI_ACCENT : 0x10161D;
            uint32_t border = i == hovered_context_item ? UI_ACCENT_ALT : UI_BORDER_SOFT;
            uint32_t text = i == hovered_context_item ? UI_TEXT_DARK : UI_TEXT;

            draw_panel_elevated_clipped(&surface, context_x + 8, item_y, CTX_MENU_W - 16, CTX_MENU_ITEM_H - 4,
                                        UI_RADIUS_SM, fill_top, fill_bottom, border,
                                        i == hovered_context_item ? UI_ACCENT_ALT : 0U,
                                        dirty_x, dirty_y, dirty_w, dirty_h);
            draw_string_clipped(&surface, context_x + 18, item_y + 7, context_menu_items[i], text,
                                dirty_x, dirty_y, dirty_w, dirty_h);
        }
    }

}

int main(void) {
    gui_create_window_params_t params;
    gui_window_info_t info;
    gui_present_params_t present;
    gui_window_event_t event;
    gui_window_event_t desktop_event;
    gui_screen_info_t screen_info;
    gui_window_snapshot_entry_t window_entries[TASK_SLOT_MAX];
    int tracked_window_count = 0;
    int handle;
    int focused = 0;
    int mouse_x = -1;
    int mouse_y = -1;
    int click_count = 0;
    int width;
    int height;
    int menu_visible = 0;
    int hovered_item = -1;
    int hovered_task_slot = -1;
    int context_visible = 0;
    int context_x = 0;
    int context_y = 0;
    int hovered_context_item = -1;
    int hovered_desktop_icon = -1;
    int drag_window_id = -1;
    int drag_off_x = 0;
    int drag_off_y = 0;
    int drag_start_x = 0;
    int drag_start_y = 0;
    int drag_start_w = 0;
    int drag_start_h = 0;
    int resize_window_id = -1;
    int resize_mode = 0;
    int resize_start_mx = 0;
    int resize_start_my = 0;
    int resize_start_x = 0;
    int resize_start_y = 0;
    int resize_start_w = 0;
    int resize_start_h = 0;
    uint32_t last_click_tick = 0;
    uint32_t last_clock_second = 0;
    int last_click_window_id = -1;
    int last_click_button = 0;
    int last_desktop_icon = -1;
    int preferred_input_window_id = -1;
    int event_from_desktop = 0;
    int surface_cache_dirty = 1;
    char status_text[96];
    char pending_open_path[256];
    uint32_t* framebuffer;
    uint32_t framebuffer_pixels;
    uint32_t last_surface_sync_tick = 0;
    int dirty_valid = 0;
    int dirty_x = 0;
    int dirty_y = 0;
    int dirty_w = 0;
    int dirty_h = 0;
    int surface_cache_flags[TASK_SLOT_MAX];
    window_surface_cache_t surface_caches[TASK_SLOT_MAX];
    window_layout_state_t layout_states[TASK_SLOT_MAX];

    desktop_icon_cache_invalidate();
    copy_text(status_text, sizeof(status_text), "");
    for (int i = 0; i < TASK_SLOT_MAX; i++) {
        surface_cache_flags[i] = 1;
        surface_caches[i].window_id = -1;
        surface_caches[i].pixels = 0;
        surface_caches[i].capacity_pixels = 0;
        surface_caches[i].width = 0;
        surface_caches[i].height = 0;
        surface_caches[i].flags = 0U;
        layout_states[i].window_id = -1;
        layout_states[i].valid = 0;
        layout_states[i].mode = WINDOW_LAYOUT_NONE;
        layout_states[i].x = 0;
        layout_states[i].y = 0;
        layout_states[i].w = 0;
        layout_states[i].h = 0;
    }
    params.size = sizeof(params);
    params.flags = GUI_WINDOW_FLAG_BORDERLESS | GUI_WINDOW_FLAG_FULLSCREEN;
    params.x = 0;
    params.y = 0;
    screen_info.size = sizeof(screen_info);
    desktop_log("[desktop] main start");
    if (user_gui_register_desktop() != 0) {
        desktop_log("[desktop] register_desktop failed");
        return 1;
    }
    desktop_log("[desktop] register_desktop ok");
    if (user_gui_get_screen_info(&screen_info) != 0) {
        desktop_log("[desktop] get_screen_info failed");
        return 1;
    }
    desktop_log("[desktop] get_screen_info ok");
    params.width = (int32_t)screen_info.width;
    params.height = (int32_t)screen_info.height;

    handle = user_gui_create_window(&params);
    if (handle <= 0) {
        desktop_log("[desktop] create_window failed");
        return 1;
    }
    desktop_log("[desktop] create_window ok");
    if (user_gui_set_title(handle, "Desktop Server") != 0) {
        desktop_log("[desktop] set_title failed");
        (void)user_gui_destroy_window(handle);
        return 1;
    }
    desktop_log("[desktop] set_title ok");
    if (user_gui_get_window_info(handle, &info) != 0) {
        desktop_log("[desktop] get_window_info failed");
        (void)user_gui_destroy_window(handle);
        return 1;
    }
    desktop_log("[desktop] get_window_info ok");

    width = info.client_width;
    height = info.client_height;
    framebuffer_pixels = (uint32_t)width * (uint32_t)height;
    framebuffer = (uint32_t*)user_malloc((size_t)framebuffer_pixels * sizeof(uint32_t));
    if (!framebuffer) {
        desktop_log("[desktop] framebuffer alloc failed");
        (void)user_gui_destroy_window(handle);
        return 1;
    }
    desktop_log("[desktop] framebuffer alloc ok");
    refresh_window_list(window_entries, &tracked_window_count);
    sync_window_surface_caches(window_entries, tracked_window_count, surface_caches, TASK_SLOT_MAX,
                               surface_cache_flags, 1);
    surface_cache_dirty = 0;
    last_surface_sync_tick = user_uptime_ticks();
    last_clock_second = last_surface_sync_tick / 100U;
    render_frame(framebuffer, width, width, height, 0, 0, width, height, focused, mouse_x, mouse_y, click_count,
                 menu_visible, hovered_item, hovered_task_slot,
                 context_visible, context_x, context_y, hovered_context_item,
                 drag_window_id, resize_window_id, hovered_desktop_icon, status_text, surface_caches, TASK_SLOT_MAX,
                 window_entries, tracked_window_count);
    present.size = sizeof(present);
    present.flags = 0U;
    present.buffer_ptr = (uintptr_t)framebuffer;
    present.x = 0;
    present.y = 0;
    present.width = (uint32_t)width;
    present.height = (uint32_t)height;
    present.stride_bytes = (uint32_t)width * 4U;
    if (user_gui_present(handle, &present) != 0) {
        desktop_log("[desktop] initial present failed");
        user_free(framebuffer);
        (void)user_gui_destroy_window(handle);
        return 1;
    }
    desktop_log("[desktop] initial present ok");
    for (;;) {
        int redraw = 0;
        int yield_after_dispatch = 0;
        int processed_events = 0;

        dirty_valid = 0;

        for (int batch = 0; batch < 8; batch++) {
            int status = user_gui_poll_desktop_event(&desktop_event);

            event_from_desktop = 0;
            if (status < 0) {
                desktop_log("[desktop] poll_desktop_event failed");
                goto desktop_loop_exit;
            }
            if (status == 0) {
                status = user_gui_poll_event(handle, &event);
                if (status == 0) {
                    if (processed_events == 0) {
                        uint32_t now_second = user_uptime_ticks() / 100U;

                        if (now_second != last_clock_second) {
                            last_clock_second = now_second;
                            redraw = 1;
                            mark_clock_dirty(&dirty_valid, &dirty_x, &dirty_y, &dirty_w, &dirty_h, width, height);
                            break;
                        }
                        user_yield();
                        goto desktop_loop_continue;
                    }
                    break;
                }
            } else {
                event = desktop_event;
                event_from_desktop = 1;
            }

            if (status < 0) {
                desktop_log("[desktop] poll_event failed");
                goto desktop_loop_exit;
            }

            processed_events++;
            switch (event.type) {
            case GUI_WIN_EVT_PAINT:
                if (event_from_desktop && event.arg2 > 0) {
                    int paint_idx = find_window_index_by_id(window_entries, tracked_window_count, event.arg2);

                    if (paint_idx == -1) {
                        refresh_window_list(window_entries, &tracked_window_count);
                        paint_idx = find_window_index_by_id(window_entries, tracked_window_count, event.arg2);
                    }
                    if (paint_idx != -1) {
                        surface_cache_dirty = 1;
                        mark_window_cache_dirty(window_entries, tracked_window_count,
                                                surface_cache_flags, TASK_SLOT_MAX, event.arg2);
                        redraw = 1;
                        mark_dirty_rect(&dirty_valid, &dirty_x, &dirty_y, &dirty_w, &dirty_h,
                                        window_entries[paint_idx].x - 8, window_entries[paint_idx].y - 8,
                                        window_entries[paint_idx].width + 16, window_entries[paint_idx].height + 16,
                                        width, height);
                    }
                } else {
                    refresh_window_list(window_entries, &tracked_window_count);
                    surface_cache_dirty = 1;
                    mark_all_window_caches_dirty(surface_cache_flags, TASK_SLOT_MAX);
                    redraw = 1;
                    mark_dirty_full(&dirty_valid, &dirty_x, &dirty_y, &dirty_w, &dirty_h, width, height);
                }
                break;
            case GUI_WIN_EVT_FOCUS_GAINED:
                focused = 1;
                refresh_window_list(window_entries, &tracked_window_count);
                surface_cache_dirty = 1;
                mark_all_window_caches_dirty(surface_cache_flags, TASK_SLOT_MAX);
                redraw = 1;
                mark_dirty_full(&dirty_valid, &dirty_x, &dirty_y, &dirty_w, &dirty_h, width, height);
                break;
            case GUI_WIN_EVT_FOCUS_LOST:
                focused = 0;
                refresh_window_list(window_entries, &tracked_window_count);
                surface_cache_dirty = 1;
                mark_all_window_caches_dirty(surface_cache_flags, TASK_SLOT_MAX);
                redraw = 1;
                mark_dirty_full(&dirty_valid, &dirty_x, &dirty_y, &dirty_w, &dirty_h, width, height);
                break;
            case GUI_WIN_EVT_MOUSE_MOVE:
                if (event_from_desktop) {
                    int hit_idx;
                    int prev_hovered_item = hovered_item;
                    int prev_hovered_task_slot = hovered_task_slot;
                    int prev_hovered_context_item = hovered_context_item;
                    int prev_hovered_desktop_icon = hovered_desktop_icon;
                    int needs_visual_redraw = 0;
                    int dispatched_to_app = 0;
                    int desktop_icon_count = 0;
                    const desktop_icon_entry_t* desktop_icons;

                    mouse_x = event.arg0;
                    mouse_y = event.arg1;
                    hovered_task_slot = find_task_slot(window_entries, tracked_window_count, width, mouse_x, mouse_y);
                    desktop_icons = desktop_icon_cache_get(width, height, &desktop_icon_count);
                    hovered_desktop_icon = hit_test_desktop_icon(desktop_icons, desktop_icon_count, mouse_x, mouse_y);
                    if (menu_visible) {
                        hovered_item = -1;
                        for (int i = 0; i < MENU_ITEMS; i++) {
                            int item_y = start_menu_item_y(i);
                            if (point_in_rect(mouse_x, mouse_y, MENU_X + 10, item_y, MENU_W - 20, MENU_ITEM_H - 6)) {
                                hovered_item = i;
                                break;
                            }
                        }
                    }
                    if (context_visible) {
                        hovered_context_item = -1;
                        for (int i = 0; i < CTX_MENU_ITEMS; i++) {
                            int item_y = context_y + 8 + i * CTX_MENU_ITEM_H;
                            if (point_in_rect(mouse_x, mouse_y, context_x + 8, item_y, CTX_MENU_W - 16, CTX_MENU_ITEM_H - 4)) {
                                hovered_context_item = i;
                                break;
                            }
                        }
                    }
                    if (drag_window_id >= 0) {
                        for (int i = 0; i < tracked_window_count; i++) {
                            if (window_entries[i].window_id == drag_window_id) {
                                int new_x = mouse_x - drag_off_x;
                                int new_y = mouse_y - drag_off_y;

                                clamp_drag_restore_rect(&new_x, &new_y,
                                                        window_entries[i].width, window_entries[i].height,
                                                        width, height);
                                if (submit_window_rect(drag_window_id, new_x, new_y,
                                                       window_entries[i].width, window_entries[i].height) == 0) {
                                    update_window_snapshot_rect(window_entries, tracked_window_count, drag_window_id,
                                                                new_x, new_y,
                                                                window_entries[i].width, window_entries[i].height);
                                    needs_visual_redraw = 1;
                                }
                                break;
                            }
                        }
                    } else if (resize_window_id >= 0) {
                        int new_x = resize_start_x;
                        int new_y = resize_start_y;
                        int new_w = resize_start_w;
                        int new_h = resize_start_h;
                        int dx = mouse_x - resize_start_mx;
                        int dy = mouse_y - resize_start_my;

                        if ((resize_mode & RESIZE_LEFT) != 0) {
                            new_x = resize_start_x + dx;
                            new_w = resize_start_w - dx;
                        }
                        if ((resize_mode & RESIZE_RIGHT) != 0) new_w = resize_start_w + dx;
                        if ((resize_mode & RESIZE_BOTTOM) != 0) new_h = resize_start_h + dy;
                        if (new_w < 120) {
                            if ((resize_mode & RESIZE_LEFT) != 0 &&
                                (resize_mode & RESIZE_RIGHT) == 0) {
                                new_x = resize_start_x + resize_start_w - 120;
                            }
                            new_w = 120;
                        }
                        if (new_h < 100) new_h = 100;
                        if (submit_window_rect(resize_window_id, new_x, new_y, new_w, new_h) == 0) {
                            update_window_snapshot_rect(window_entries, tracked_window_count, resize_window_id,
                                                        new_x, new_y, new_w, new_h);
                            surface_cache_dirty = 1;
                            mark_window_cache_dirty(window_entries, tracked_window_count,
                                                    surface_cache_flags, TASK_SLOT_MAX, resize_window_id);
                            needs_visual_redraw = 1;
                        }
                    } else {
                        hit_idx = hit_test_window(window_entries, tracked_window_count, mouse_x, mouse_y);
                        if (hit_idx != -1) {
                            if (forward_pointer_event(&window_entries[hit_idx], GUI_WIN_EVT_MOUSE_MOVE, mouse_x, mouse_y, 0) == 0) {
                                mark_window_cache_dirty(window_entries, tracked_window_count,
                                                        surface_cache_flags, TASK_SLOT_MAX,
                                                        window_entries[hit_idx].window_id);
                                dispatched_to_app = 1;
                            }
                        }
                    }
                    if (hovered_item != prev_hovered_item ||
                        hovered_task_slot != prev_hovered_task_slot ||
                        hovered_context_item != prev_hovered_context_item ||
                        hovered_desktop_icon != prev_hovered_desktop_icon) {
                        needs_visual_redraw = 1;
                    }
                    if (dispatched_to_app) yield_after_dispatch = 1;
                    redraw = needs_visual_redraw;
                    if (needs_visual_redraw) {
                        if (drag_window_id >= 0 || resize_window_id >= 0 || dispatched_to_app) {
                            mark_dirty_full(&dirty_valid, &dirty_x, &dirty_y, &dirty_w, &dirty_h, width, height);
                        } else {
                            if (hovered_task_slot != prev_hovered_task_slot) {
                                mark_taskbar_dirty(&dirty_valid, &dirty_x, &dirty_y, &dirty_w, &dirty_h, width, height);
                            }
                            if (menu_visible || hovered_item != prev_hovered_item) {
                                mark_menu_dirty(&dirty_valid, &dirty_x, &dirty_y, &dirty_w, &dirty_h, width, height);
                            }
                            if (context_visible || hovered_context_item != prev_hovered_context_item) {
                                mark_context_dirty(&dirty_valid, &dirty_x, &dirty_y, &dirty_w, &dirty_h,
                                                   width, height, context_x, context_y);
                            }
                            if (hovered_desktop_icon != prev_hovered_desktop_icon) {
                                if (prev_hovered_desktop_icon >= 0 && prev_hovered_desktop_icon < desktop_icon_count) {
                                    mark_desktop_icon_entry_dirty(&dirty_valid, &dirty_x, &dirty_y, &dirty_w, &dirty_h,
                                                                  width, height, &desktop_icons[prev_hovered_desktop_icon]);
                                }
                                if (hovered_desktop_icon >= 0 && hovered_desktop_icon < desktop_icon_count) {
                                    mark_desktop_icon_entry_dirty(&dirty_valid, &dirty_x, &dirty_y, &dirty_w, &dirty_h,
                                                                  width, height, &desktop_icons[hovered_desktop_icon]);
                                }
                            }
                        }
                    }
                }
                break;
            case GUI_WIN_EVT_MOUSE_DOWN:
                if (event_from_desktop) {
                    int hit_idx;

                    mouse_x = event.arg0;
                    mouse_y = event.arg1;
                    click_count++;
                    if (event.arg2 == 2 &&
                        !point_in_rect(mouse_x, mouse_y, 0, 0, width, TASKBAR_HEIGHT) &&
                        hit_test_window(window_entries, tracked_window_count, mouse_x, mouse_y) == -1) {
                        context_visible = 1;
                        context_x = mouse_x;
                        context_y = mouse_y;
                        if (context_x + CTX_MENU_W > width) context_x = width - CTX_MENU_W - 8;
                        if (context_x < 8) context_x = 8;
                        if (context_y + 12 + CTX_MENU_ITEMS * CTX_MENU_ITEM_H + 10 > height) {
                            context_y = height - (12 + CTX_MENU_ITEMS * CTX_MENU_ITEM_H + 10) - 8;
                        }
                        if (context_y < TASKBAR_HEIGHT + 4) context_y = TASKBAR_HEIGHT + 4;
                        hovered_context_item = -1;
                        menu_visible = 0;
                        hovered_item = -1;
                    } else if (event.arg2 == 1 &&
                               point_in_rect(mouse_x, mouse_y, START_BUTTON_X, START_BUTTON_Y, START_BUTTON_W, START_BUTTON_H)) {
                        menu_visible = !menu_visible;
                        if (!menu_visible) hovered_item = -1;
                        context_visible = 0;
                        hovered_context_item = -1;
                    } else if (event.arg2 == 1 &&
                               point_in_rect(mouse_x, mouse_y,
                                             START_BUTTON_X + START_BUTTON_W + 10, START_BUTTON_Y,
                                             APP_BUTTON_W, START_BUTTON_H)) {
                        launch_terminal_action(status_text, sizeof(status_text));
                        refresh_window_list(window_entries, &tracked_window_count);
                        surface_cache_dirty = 1;
                        mark_all_window_caches_dirty(surface_cache_flags, TASK_SLOT_MAX);
                        menu_visible = 0;
                        hovered_item = -1;
                        context_visible = 0;
                        hovered_context_item = -1;
                    } else if (event.arg2 == 1 &&
                               point_in_rect(mouse_x, mouse_y,
                                             width - CLOCK_BOX_W - TASKBAR_NET_W - SETTINGS_BUTTON_W - 28, START_BUTTON_Y,
                                             SETTINGS_BUTTON_W, START_BUTTON_H)) {
                        launch_menu_action(1, status_text, sizeof(status_text));
                        refresh_window_list(window_entries, &tracked_window_count);
                        surface_cache_dirty = 1;
                        mark_all_window_caches_dirty(surface_cache_flags, TASK_SLOT_MAX);
                        menu_visible = 0;
                        hovered_item = -1;
                        context_visible = 0;
                        hovered_context_item = -1;
                    } else if (event.arg2 == 1 &&
                               (hovered_task_slot = find_task_slot(window_entries, tracked_window_count, width, mouse_x, mouse_y)) != -1 &&
                               hovered_task_slot < tracked_window_count) {
                        if ((window_entries[hovered_task_slot].flags & GUI_WINDOW_SNAPSHOT_MINIMIZED) != 0U) {
                            submit_window_command(window_entries[hovered_task_slot].window_id, GUI_DESKTOP_WINDOW_RESTORE);
                            preferred_input_window_id = window_entries[hovered_task_slot].window_id;
                            copy_text(status_text, sizeof(status_text), "Window restored");
                        } else if ((window_entries[hovered_task_slot].flags & GUI_WINDOW_SNAPSHOT_ACTIVE) != 0U) {
                            submit_window_command(window_entries[hovered_task_slot].window_id, GUI_DESKTOP_WINDOW_MINIMIZE);
                            if (preferred_input_window_id == window_entries[hovered_task_slot].window_id) preferred_input_window_id = -1;
                            copy_text(status_text, sizeof(status_text), "Window minimized");
                        } else {
                            submit_window_command(window_entries[hovered_task_slot].window_id, GUI_DESKTOP_WINDOW_FOCUS);
                            preferred_input_window_id = window_entries[hovered_task_slot].window_id;
                            copy_text(status_text, sizeof(status_text), "Window focused");
                        }
                        refresh_window_list(window_entries, &tracked_window_count);
                        surface_cache_dirty = 1;
                        mark_all_window_caches_dirty(surface_cache_flags, TASK_SLOT_MAX);
                        menu_visible = 0;
                        hovered_item = -1;
                        context_visible = 0;
                        hovered_context_item = -1;
                    } else if (event.arg2 == 1 && menu_visible && hovered_item != -1) {
                        launch_menu_action(hovered_item, status_text, sizeof(status_text));
                        menu_visible = 0;
                        hovered_item = -1;
                        context_visible = 0;
                        hovered_context_item = -1;
                        refresh_window_list(window_entries, &tracked_window_count);
                        surface_cache_dirty = 1;
                        mark_all_window_caches_dirty(surface_cache_flags, TASK_SLOT_MAX);
                    } else if (event.arg2 == 1 && context_visible && hovered_context_item != -1) {
                        launch_context_action(hovered_context_item, status_text, sizeof(status_text));
                        context_visible = 0;
                        hovered_context_item = -1;
                        refresh_window_list(window_entries, &tracked_window_count);
                        surface_cache_dirty = 1;
                        mark_all_window_caches_dirty(surface_cache_flags, TASK_SLOT_MAX);
                    } else if (menu_visible && point_in_rect(mouse_x, mouse_y, MENU_X, MENU_Y, MENU_W, start_menu_height())) {
                        copy_text(status_text, sizeof(status_text), "");
                    } else if (context_visible && point_in_rect(mouse_x, mouse_y, context_x, context_y,
                                                               CTX_MENU_W, 12 + CTX_MENU_ITEMS * CTX_MENU_ITEM_H + 10)) {
                        copy_text(status_text, sizeof(status_text), "");
                    } else if (event.arg2 == 1 && hovered_desktop_icon != -1) {
                        uint32_t now = user_uptime_ticks();
                        int double_click = last_desktop_icon == hovered_desktop_icon &&
                                           now - last_click_tick < DESKTOP_DOUBLE_CLICK_TICKS;
                        int desktop_icon_count = 0;
                        const desktop_icon_entry_t* desktop_icons = desktop_icon_cache_get(width, height, &desktop_icon_count);

                        if (hovered_desktop_icon < desktop_icon_count) {
                            copy_text(status_text, sizeof(status_text), desktop_icons[hovered_desktop_icon].label);
                            if (double_click) {
                                activate_desktop_icon(&desktop_icons[hovered_desktop_icon], status_text, sizeof(status_text));
                                refresh_window_list(window_entries, &tracked_window_count);
                                surface_cache_dirty = 1;
                                mark_all_window_caches_dirty(surface_cache_flags, TASK_SLOT_MAX);
                            }
                        }
                        last_desktop_icon = hovered_desktop_icon;
                        last_click_tick = now;
                        menu_visible = 0;
                        hovered_item = -1;
                        context_visible = 0;
                        hovered_context_item = -1;
                    } else {
                        hit_idx = hit_test_window(window_entries, tracked_window_count, mouse_x, mouse_y);
                        menu_visible = 0;
                        hovered_item = -1;
                        context_visible = 0;
                        hovered_context_item = -1;
                        if (hit_idx != -1) {
                            int target_window_id = window_entries[hit_idx].window_id;
                            int resize_flags = event.arg2 == 1 ? hit_test_resize(&window_entries[hit_idx], mouse_x, mouse_y) : 0;
                            int control_action = event.arg2 == 1 ?
                                                 hit_test_window_control(&window_entries[hit_idx], mouse_x, mouse_y) :
                                                 WINDOW_CTRL_NONE;
                            const gui_window_snapshot_entry_t* target_window = 0;

                            submit_window_command(target_window_id, GUI_DESKTOP_WINDOW_FOCUS);
                            preferred_input_window_id = target_window_id;
                            refresh_window_list(window_entries, &tracked_window_count);
                            surface_cache_dirty = 1;
                            mark_all_window_caches_dirty(surface_cache_flags, TASK_SLOT_MAX);
                            for (int i = 0; i < tracked_window_count; i++) {
                                if (window_entries[i].window_id == target_window_id) {
                                    target_window = &window_entries[i];
                                    break;
                                }
                            }
                            if (!target_window) break;
                            {
                                int titlebar_hit = point_in_rect(mouse_x, mouse_y, target_window->x, target_window->y,
                                                                 target_window->width, WINDOW_CAPTION_H);

                            if (control_action == WINDOW_CTRL_CLOSE) {
                                submit_window_command(target_window_id, GUI_DESKTOP_WINDOW_CLOSE_REQUEST);
                                forget_layout_state(layout_states, TASK_SLOT_MAX, target_window_id);
                                if (preferred_input_window_id == target_window_id) preferred_input_window_id = -1;
                                refresh_window_list(window_entries, &tracked_window_count);
                                surface_cache_dirty = 1;
                                mark_all_window_caches_dirty(surface_cache_flags, TASK_SLOT_MAX);
                                drag_window_id = -1;
                                resize_window_id = -1;
                                resize_mode = 0;
                                copy_text(status_text, sizeof(status_text), "Window closed");
                            } else if (control_action == WINDOW_CTRL_MINIMIZE) {
                                submit_window_command(target_window_id, GUI_DESKTOP_WINDOW_MINIMIZE);
                                if (preferred_input_window_id == target_window_id) preferred_input_window_id = -1;
                                refresh_window_list(window_entries, &tracked_window_count);
                                surface_cache_dirty = 1;
                                mark_all_window_caches_dirty(surface_cache_flags, TASK_SLOT_MAX);
                                drag_window_id = -1;
                                resize_window_id = -1;
                                resize_mode = 0;
                                copy_text(status_text, sizeof(status_text), "Window minimized");
                            } else if (control_action == WINDOW_CTRL_MAXIMIZE) {
                                int layout_rc = toggle_window_maximize(layout_states, TASK_SLOT_MAX,
                                                                       window_entries, tracked_window_count,
                                                                       target_window, width, height);

                                refresh_window_list(window_entries, &tracked_window_count);
                                surface_cache_dirty = 1;
                                mark_all_window_caches_dirty(surface_cache_flags, TASK_SLOT_MAX);
                                drag_window_id = -1;
                                resize_window_id = -1;
                                resize_mode = 0;
                                copy_text(status_text, sizeof(status_text),
                                          layout_rc == 1 ? "Window restored" :
                                          layout_rc == 0 ? "Window maximized" : "Maximize failed");
                            } else if (resize_flags != 0) {
                                forget_layout_state(layout_states, TASK_SLOT_MAX, target_window_id);
                                resize_window_id = target_window_id;
                                resize_mode = resize_flags;
                                resize_start_mx = mouse_x;
                                resize_start_my = mouse_y;
                                resize_start_x = target_window->x;
                                resize_start_y = target_window->y;
                                resize_start_w = target_window->width;
                                resize_start_h = target_window->height;
                                drag_window_id = -1;
                                copy_text(status_text, sizeof(status_text), "Resizing window");
                            } else if (event.arg2 == 1 && titlebar_hit) {
                                if (restore_layout_before_drag(layout_states, TASK_SLOT_MAX,
                                                               window_entries, tracked_window_count,
                                                               target_window_id, mouse_x, mouse_y,
                                                               width, height)) {
                                    surface_cache_dirty = 1;
                                    mark_all_window_caches_dirty(surface_cache_flags, TASK_SLOT_MAX);
                                }
                                drag_window_id = target_window_id;
                                drag_off_x = mouse_x - target_window->x;
                                drag_off_y = mouse_y - target_window->y;
                                drag_start_x = target_window->x;
                                drag_start_y = target_window->y;
                                drag_start_w = target_window->width;
                                drag_start_h = target_window->height;
                                resize_window_id = -1;
                                resize_mode = 0;
                                copy_text(status_text, sizeof(status_text), "Dragging window");
                            } else {
                                int click_semantics = event.arg2;

                                if (event.arg2 == 1 && window_accepts_client_input(target_window)) {
                                    uint32_t now = user_uptime_ticks();

                                    if (last_click_window_id == target_window_id &&
                                        last_click_button == event.arg2 &&
                                        now - last_click_tick < DESKTOP_DOUBLE_CLICK_TICKS) {
                                        click_semantics = 2;
                                    }
                                    last_click_window_id = target_window_id;
                                    last_click_button = event.arg2;
                                    last_click_tick = now;
                                } else {
                                    last_click_window_id = -1;
                                    last_click_button = 0;
                                    last_click_tick = 0;
                                }
                                if (forward_pointer_event(target_window, GUI_WIN_EVT_MOUSE_DOWN,
                                                          mouse_x, mouse_y, click_semantics) == 0) {
                                    surface_cache_dirty = 1;
                                    mark_window_cache_dirty(window_entries, tracked_window_count,
                                                            surface_cache_flags, TASK_SLOT_MAX, target_window_id);
                                    yield_after_dispatch = 1;
                                }
                            }
                            }
                        }
                    }
                    redraw = 1;
                }
                break;
            case GUI_WIN_EVT_MOUSE_UP:
                if (event_from_desktop && event.arg2 == 0) {
                    mouse_x = event.arg0;
                    mouse_y = event.arg1;
                    int hit_idx = hit_test_window(window_entries, tracked_window_count, mouse_x, mouse_y);
                    int was_dragging = drag_window_id >= 0;
                    int was_resizing = resize_window_id >= 0;
                    int layout_status_set = 0;

                    if (!was_dragging && !was_resizing && hit_idx != -1) {
                        if (forward_pointer_event(&window_entries[hit_idx], GUI_WIN_EVT_MOUSE_UP,
                                                  mouse_x, mouse_y, 0) == 0) {
                            surface_cache_dirty = 1;
                            mark_window_cache_dirty(window_entries, tracked_window_count,
                                                    surface_cache_flags, TASK_SLOT_MAX,
                                                    window_entries[hit_idx].window_id);
                            yield_after_dispatch = 1;
                        }
                    }
                    if (was_dragging) {
                        int drag_idx = find_window_index_by_id(window_entries, tracked_window_count, drag_window_id);
                        int snap_mode = WINDOW_LAYOUT_NONE;

                        if (mouse_x <= WINDOW_SNAP_MARGIN) snap_mode = WINDOW_LAYOUT_SNAP_LEFT;
                        else if (mouse_x >= width - WINDOW_SNAP_MARGIN) snap_mode = WINDOW_LAYOUT_SNAP_RIGHT;

                        if (drag_idx != -1 && snap_mode != WINDOW_LAYOUT_NONE) {
                            if (apply_window_layout(layout_states, TASK_SLOT_MAX,
                                                    window_entries, tracked_window_count,
                                                    &window_entries[drag_idx], snap_mode,
                                                    width, height,
                                                    drag_start_x, drag_start_y, drag_start_w, drag_start_h) == 0) {
                                refresh_window_list(window_entries, &tracked_window_count);
                                surface_cache_dirty = 1;
                                mark_all_window_caches_dirty(surface_cache_flags, TASK_SLOT_MAX);
                                copy_text(status_text, sizeof(status_text),
                                          snap_mode == WINDOW_LAYOUT_SNAP_LEFT ? "Window snapped left" :
                                          snap_mode == WINDOW_LAYOUT_SNAP_RIGHT ? "Window snapped right" :
                                          "Window maximized");
                                layout_status_set = 1;
                            }
                        } else {
                            forget_layout_state(layout_states, TASK_SLOT_MAX, drag_window_id);
                        }
                    }
                    drag_window_id = -1;
                    resize_window_id = -1;
                    resize_mode = 0;
                    if (!layout_status_set) copy_text(status_text, sizeof(status_text), "");
                    redraw = 1;
                }
                break;
            case GUI_WIN_EVT_MOUSE_WHEEL:
                if (event_from_desktop) {
                    int hit_idx;

                    mouse_x = event.arg0;
                    mouse_y = event.arg1;
                    hit_idx = hit_test_window(window_entries, tracked_window_count, mouse_x, mouse_y);
                    if (hit_idx != -1) {
                        if (forward_pointer_event(&window_entries[hit_idx], GUI_WIN_EVT_MOUSE_WHEEL,
                                                  mouse_x, mouse_y, event.arg2) == 0) {
                            surface_cache_dirty = 1;
                            mark_window_cache_dirty(window_entries, tracked_window_count,
                                                    surface_cache_flags, TASK_SLOT_MAX,
                                                    window_entries[hit_idx].window_id);
                            yield_after_dispatch = 1;
                        }
                    }
                    redraw = 1;
                }
                break;
            case GUI_WIN_EVT_KEY_DOWN:
                if (event_from_desktop) {
                    int active_idx = resolve_input_window(window_entries, tracked_window_count, preferred_input_window_id);

                    if (event.arg0 == 0x01) {
                        menu_visible = 0;
                        hovered_item = -1;
                        context_visible = 0;
                        hovered_context_item = -1;
                        drag_window_id = -1;
                        resize_window_id = -1;
                        resize_mode = 0;
                        copy_text(status_text, sizeof(status_text), "Menu dismissed");
                        redraw = 1;
                    } else if (active_idx != -1) {
                        if (submit_window_input(window_entries[active_idx].window_id, GUI_WIN_EVT_KEY_DOWN,
                                                event.arg0, event.arg1, event.arg2) == 0) {
                            surface_cache_dirty = 1;
                            mark_window_cache_dirty(window_entries, tracked_window_count,
                                                    surface_cache_flags, TASK_SLOT_MAX,
                                                    window_entries[active_idx].window_id);
                            redraw = 1;
                            yield_after_dispatch = 1;
                        }
                    }
                }
                break;
            case GUI_WIN_EVT_KEY_UP:
                if (event_from_desktop) {
                    int active_idx = resolve_input_window(window_entries, tracked_window_count, preferred_input_window_id);

                    if (active_idx != -1) {
                        if (submit_window_input(window_entries[active_idx].window_id, GUI_WIN_EVT_KEY_UP,
                                                event.arg0, event.arg1, event.arg2) == 0) {
                            surface_cache_dirty = 1;
                            mark_window_cache_dirty(window_entries, tracked_window_count,
                                                    surface_cache_flags, TASK_SLOT_MAX,
                                                    window_entries[active_idx].window_id);
                            redraw = 1;
                            yield_after_dispatch = 1;
                        }
                    }
                }
                break;
            case GUI_WIN_EVT_CHAR:
                if (event_from_desktop) {
                    int active_idx = resolve_input_window(window_entries, tracked_window_count, preferred_input_window_id);

                    if (active_idx != -1) {
                        if (submit_window_input(window_entries[active_idx].window_id, GUI_WIN_EVT_CHAR,
                                                event.arg0, event.arg1, event.arg2) == 0) {
                            surface_cache_dirty = 1;
                            mark_window_cache_dirty(window_entries, tracked_window_count,
                                                    surface_cache_flags, TASK_SLOT_MAX,
                                                    window_entries[active_idx].window_id);
                            redraw = 1;
                            yield_after_dispatch = 1;
                        }
                    }
                }
                break;
            case GUI_WIN_EVT_WINDOW_RESIZED:
                if (user_gui_get_window_info(handle, &info) == 0) {
                    width = info.client_width;
                    height = info.client_height;
                    user_free(framebuffer);
                    framebuffer_pixels = (uint32_t)width * (uint32_t)height;
                    framebuffer = (uint32_t*)user_malloc((size_t)framebuffer_pixels * sizeof(uint32_t));
                    if (!framebuffer) {
                        desktop_log("[desktop] resize framebuffer alloc failed");
                        (void)user_gui_destroy_window(handle);
                        return 1;
                    }
                    present.buffer_ptr = (uintptr_t)framebuffer;
                    present.stride_bytes = (uint32_t)width * 4U;
                    desktop_icon_cache_invalidate();
                    refresh_window_list(window_entries, &tracked_window_count);
                    surface_cache_dirty = 1;
                    mark_all_window_caches_dirty(surface_cache_flags, TASK_SLOT_MAX);
                    redraw = 1;
                }
                break;
            case GUI_WIN_EVT_DESKTOP_OPEN_PATH:
                if (event_from_desktop &&
                    user_gui_consume_open_path(pending_open_path, (uint32_t)sizeof(pending_open_path)) == 0) {
                    const char* narcpad_argv[2];

                    narcpad_argv[0] = "/bin/narcpad";
                    narcpad_argv[1] = pending_open_path;
                    if (user_spawn("/bin/narcpad", narcpad_argv, 2U) >= 0) {
                        copy_text(status_text, sizeof(status_text), "Desktop opened file request");
                        refresh_window_list(window_entries, &tracked_window_count);
                        surface_cache_dirty = 1;
                        mark_all_window_caches_dirty(surface_cache_flags, TASK_SLOT_MAX);
                        redraw = 1;
                    }
                }
                break;
            case GUI_WIN_EVT_CLOSE_REQUEST:
                desktop_log("[desktop] close_request ignored");
                redraw = 1;
                mark_dirty_full(&dirty_valid, &dirty_x, &dirty_y, &dirty_w, &dirty_h, width, height);
                break;
            default:
                break;
            }
        }

        if (redraw) {
            uint32_t now = user_uptime_ticks();

            if (!dirty_valid) {
                mark_dirty_full(&dirty_valid, &dirty_x, &dirty_y, &dirty_w, &dirty_h, width, height);
            }

            if (preferred_input_window_id != -1 &&
                find_window_index_by_id(window_entries, tracked_window_count, preferred_input_window_id) == -1) {
                preferred_input_window_id = -1;
            }

            if (surface_cache_dirty || now - last_surface_sync_tick >= 10U) {
                if (!surface_cache_dirty && drag_window_id < 0 && resize_window_id < 0 &&
                    menu_visible == 0 && context_visible == 0) {
                    last_surface_sync_tick = now;
                } else {
                    sync_window_surface_caches(window_entries, tracked_window_count, surface_caches, TASK_SLOT_MAX,
                                               surface_cache_flags, 0);
                    surface_cache_dirty = 0;
                    last_surface_sync_tick = now;
                }
            }
            render_frame(framebuffer, width, width, height, dirty_x, dirty_y, dirty_w, dirty_h,
                         focused, mouse_x, mouse_y, click_count,
                         menu_visible, hovered_item, hovered_task_slot,
                         context_visible, context_x, context_y, hovered_context_item,
                         drag_window_id, resize_window_id, hovered_desktop_icon, status_text, surface_caches, TASK_SLOT_MAX,
                         window_entries, tracked_window_count);
            present.x = dirty_x;
            present.y = dirty_y;
            present.buffer_ptr = (uintptr_t)(framebuffer + ((size_t)dirty_y * (size_t)width + (size_t)dirty_x));
            present.width = (uint32_t)dirty_w;
            present.height = (uint32_t)dirty_h;
            if (user_gui_present(handle, &present) != 0) {
                desktop_log("[desktop] present failed");
                break;
            }
            last_clock_second = user_uptime_ticks() / 100U;
        }
        if (yield_after_dispatch) user_yield();
desktop_loop_continue:
        continue;
    }

desktop_loop_exit:
    desktop_log("[desktop] main loop exit");
    for (int i = 0; i < TASK_SLOT_MAX; i++) release_window_surface_cache(&surface_caches[i]);
    user_free(framebuffer);
    (void)user_gui_destroy_window(handle);
    return 1;
}
