#ifndef VBE_H
#define VBE_H
#include <stdint.h>

typedef enum {
    WIN_TYPE_TERMINAL,
    WIN_TYPE_EXPLORER,
    WIN_TYPE_NARCPAD,
    WIN_TYPE_SNAKE,
    WIN_TYPE_SETTINGS
} window_type_t;

typedef struct {
    window_type_t type;
    int x, y, w, h;
    char title[32];
    int visible;
    int minimized;
    int id;
} window_t;

#define MAX_WINDOWS 8
void init_vbe();
void vbe_update();
void vbe_put_pixel(int x, int y, uint32_t color);
uint32_t vbe_get_pixel(int x, int y);
void vbe_clear(uint32_t color);
void vbe_draw_char_hd(int x, int y, char c, uint32_t color, int scale);
void vbe_draw_char(int x, int y, char c, uint32_t color);
void vbe_draw_string_hd(int x, int y, const char* s, uint32_t color, int scale);
void vbe_draw_string(int x, int y, const char* s, uint32_t color);
void vbe_fill_rect(int x, int y, int w, int h, uint32_t color);
void vbe_fill_rect_alpha(int x, int y, int w, int h, uint32_t color, int alpha);
void vbe_fill_rect_gradient(int x, int y, int w, int h, uint32_t c1, uint32_t c2, int vertical);
void vbe_draw_rect(int x, int y, int w, int h, uint32_t color);
void vbe_draw_wallpaper();
void vbe_draw_cursor(int x, int y);
void vbe_render_mouse(int x, int y);
void vbe_render_mouse_direct(int x, int y);
void* vbe_get_backbuffer();
void* vbe_get_window_buffer();
void vbe_set_target(uint8_t* buffer, uint32_t width);
void vbe_compose_scene(window_t* windows, int win_count, int active_win_idx, int start_vis, int desktop_dir, int drag_file_idx, int mx, int my, int ctx_vis, int ctx_x, int ctx_y, const char** ctx_items, int ctx_count, int ctx_sel);
void vbe_draw_desktop_icons(int desktop_dir);
void vbe_draw_context_menu(int x, int y, const char** items, int count, int selected_idx);
void vbe_draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color, int alpha);
void vbe_draw_shadow(int x, int y, int w, int h, int radius);
uint32_t vbe_mix_color(uint32_t c1, uint32_t c2, int alpha);
void vbe_prepare_frame_from_composition();
void vbe_present_cursor_fast(int old_x, int old_y, int new_x, int new_y);
void wait_vsync();
extern volatile int gui_needs_redraw;
void vbe_memcpy(void* dest, void* src, uint32_t count);
extern void vbe_memcpy_sse(void* dest, void* src, uint32_t count);
extern void vbe_memset_sse(void* dest, uint32_t color, uint32_t count);
extern void vbe_alpha_blend_sse(void* dest, uint32_t color, uint32_t alpha, uint32_t count);
void vbe_blit_window(window_t* win, uint8_t* win_buf, int is_focused);
void vbe_get_window_client_rect(window_t* win, int* out_x, int* out_y, int* out_w, int* out_h);
void vbe_draw_taskbar(int start_btn_active);
void vbe_draw_start_menu();
void vbe_draw_clock();
void vbe_draw_icon(int x, int y, int type, const char* label, int selected);
void vbe_draw_vector_folder(int x, int y, int selected);
void vbe_draw_vector_file(int x, int y, int selected);
void vbe_draw_vector_pc(int x, int y);
void vbe_draw_vector_snake(int x, int y);
void vbe_draw_vector_terminal(int x, int y);
void vbe_draw_explorer_content(int x, int y, int w, int h, int current_dir);
void vbe_draw_breadcrumb(int x, int y, int w, int current_dir);
void vbe_draw_narcpad(int x, int y, int w, int h, const char* title, const char* content);
void vbe_draw_snake_game(int x, int y, int w, int h, int* px, int* py, int len, int ax, int ay, int dead, int score, int best);
void vbe_blit_rect(int x, int y, int w, int h, uint8_t* src_buf, uint32_t src_stride);
uint32_t vbe_get_width();
uint32_t vbe_get_height();
uint32_t vbe_get_bpp();
#endif
