#ifndef VBE_H
#define VBE_H
#include <stdint.h>
void init_vbe();
void vbe_update();
void vbe_put_pixel(int x, int y, uint32_t color);
uint32_t vbe_get_pixel(int x, int y);
void vbe_clear(uint32_t color);
void vbe_draw_char(int x, int y, char c, uint32_t color);
void vbe_draw_string(int x, int y, const char* s, uint32_t color);
void vbe_fill_rect(int x, int y, int w, int h, uint32_t color);
void vbe_draw_rect(int x, int y, int w, int h, uint32_t color);
void vbe_draw_wallpaper();
void vbe_draw_cursor(int x, int y);
void vbe_render_mouse(int x, int y);
void vbe_render_mouse_direct(int x, int y);
void* vbe_get_backbuffer();
void* vbe_get_window_buffer();
void vbe_set_target(uint8_t* buffer, uint32_t width);
void vbe_compose_scene(int wx, int wy, int win_vis, int start_vis, int exp_vis, int exp_x, int exp_y, int exp_dir);
void vbe_prepare_frame_from_composition();
void wait_vsync();
extern volatile int gui_needs_redraw;
void vbe_memcpy(void* dest, void* src, uint32_t count);
void vbe_memcpy_sse(void* dest, void* src, uint32_t count);
void vbe_blit_window(int x, int y, int w, int h, uint8_t* win_buf);
void vbe_draw_taskbar(int start_btn_active);
void vbe_draw_start_menu();
void vbe_draw_clock();
void vbe_draw_icon(int x, int y, int type, const char* label, int selected);
void vbe_draw_explorer(int wx, int wy, int current_dir);
void vbe_blit_rect(int x, int y, int w, int h, uint8_t* src_buf, uint32_t src_stride);
uint32_t vbe_get_width();
uint32_t vbe_get_height();
uint32_t vbe_get_bpp();
#endif
