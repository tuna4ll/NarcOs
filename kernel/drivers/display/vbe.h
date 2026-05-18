#ifndef VBE_H
#define VBE_H
#include <stdint.h>
#include "gui_events.h"

#define WINDOW_SURFACE_DAMAGE_RECT_CAP 4

typedef enum {
    WIN_TYPE_TERMINAL,
    WIN_TYPE_USER
} window_type_t;

typedef enum {
    CURSOR_MODE_ARROW = 0,
    CURSOR_MODE_RESIZE_H,
    CURSOR_MODE_RESIZE_V,
    CURSOR_MODE_RESIZE_DIAG_LR,
    CURSOR_MODE_RESIZE_DIAG_RL
} cursor_mode_t;

typedef struct {
    window_type_t type;
    int x, y, w, h;
    char title[32];
    int visible;
    int minimized;
    int id;
    int owner_pid;
    uint32_t flags;
    uint8_t* client_surface;
    uint32_t client_surface_pages;
    uint32_t client_surface_w;
    uint32_t client_surface_h;
    uint32_t client_surface_bpp;
    uint8_t client_damage_count;
    int client_damage_x[WINDOW_SURFACE_DAMAGE_RECT_CAP];
    int client_damage_y[WINDOW_SURFACE_DAMAGE_RECT_CAP];
    int client_damage_w[WINDOW_SURFACE_DAMAGE_RECT_CAP];
    int client_damage_h[WINDOW_SURFACE_DAMAGE_RECT_CAP];
    uint16_t event_head;
    uint16_t event_tail;
    gui_window_event_t event_queue[WINDOW_EVENT_QUEUE_CAP];
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
void vbe_set_cursor_mode(cursor_mode_t mode);
void vbe_render_mouse(int x, int y);
void vbe_render_mouse_direct(int x, int y);
void* vbe_get_backbuffer();
void* vbe_get_window_buffer();
void vbe_set_target(uint8_t* buffer, uint32_t width, uint32_t height);
void vbe_compose_scene(window_t* windows, int win_count, int active_win_idx, int start_vis, int desktop_dir, int drag_file_idx, int mx, int my, int ctx_vis, int ctx_x, int ctx_y, const char** ctx_items, int ctx_count, int ctx_sel);
void vbe_compose_scene_region(window_t* windows, int win_count, int active_win_idx, int start_vis, int desktop_dir, int drag_file_idx, int mx, int my, int ctx_vis, int ctx_x, int ctx_y, const char** ctx_items, int ctx_count, int ctx_sel, int dirty_x, int dirty_y, int dirty_w, int dirty_h);
void vbe_present_composition_with_cursor(int mx, int my);
void vbe_present_composition_region(int x, int y, int w, int h);
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

int nwm_get_idx_by_type(window_type_t type);
int nwm_create_user_window(int owner_pid, const gui_create_window_params_t* params);
int nwm_destroy_user_window(int owner_pid, int window_id);
int nwm_set_user_window_title(int owner_pid, int window_id, const char* title);
int nwm_present_user_window(int owner_pid, int window_id, const gui_present_params_t* params);
int nwm_get_user_window_info(int owner_pid, int window_id, gui_window_info_t* out_info);
int nwm_poll_user_window_event(int owner_pid, int window_id, gui_window_event_t* out_event);
int nwm_get_screen_info(gui_screen_info_t* out_info);
int nwm_register_desktop_owner(int owner_pid);
void nwm_release_desktop_owner(int owner_pid);
int nwm_get_desktop_owner_pid(void);
int nwm_desktop_events_pending(void);
int nwm_desktop_surface_active_state(void);
int nwm_poll_desktop_event(int owner_pid, gui_window_event_t* out_event);
void nwm_queue_desktop_event(uint16_t type, int16_t arg0, int16_t arg1, int32_t arg2);
int nwm_list_windows_for_desktop(int owner_pid, gui_window_snapshot_entry_t* out_entries, int max_entries);
int nwm_read_window_surface_for_desktop(int owner_pid, gui_window_surface_read_t* io);
int nwm_desktop_window_action(int owner_pid, const gui_desktop_window_action_t* action);
void nwm_close_windows_for_owner(int owner_pid);
void nwm_queue_active_window_key_event(int keycode, int modifiers);
void nwm_queue_active_window_key_up_event(int keycode, int modifiers);
void nwm_queue_active_window_char_event(char c);
void nwm_queue_active_window_mouse_move(int mx, int my);
#endif
