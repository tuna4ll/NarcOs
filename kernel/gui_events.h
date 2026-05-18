#ifndef GUI_EVENTS_H
#define GUI_EVENTS_H

#include <stdint.h>

#define WINDOW_EVENT_QUEUE_CAP 64
#define GUI_PIXEL_FORMAT_XRGB8888 1U
#define GUI_WINDOW_FLAG_BORDERLESS 0x00000001U
#define GUI_WINDOW_FLAG_FULLSCREEN 0x00000002U
#define GUI_WINDOW_SNAPSHOT_ACTIVE 0x00000001U
#define GUI_WINDOW_SNAPSHOT_VISIBLE 0x00000002U
#define GUI_WINDOW_SNAPSHOT_MINIMIZED 0x00000004U
#define GUI_WINDOW_SNAPSHOT_BORDERLESS 0x00000008U
#define GUI_WINDOW_SNAPSHOT_FULLSCREEN 0x00000010U
#define GUI_WINDOW_SNAPSHOT_DESKTOP 0x00000020U
#define GUI_WINDOW_SNAPSHOT_USER 0x00000040U
#define GUI_PRESENT_FLAG_STRETCH_CLIENT 0x00000001U

#define GUI_DESKTOP_WINDOW_FOCUS 1U
#define GUI_DESKTOP_WINDOW_MINIMIZE 2U
#define GUI_DESKTOP_WINDOW_RESTORE 3U
#define GUI_DESKTOP_WINDOW_SET_RECT 4U
#define GUI_DESKTOP_WINDOW_CLOSE_REQUEST 5U
#define GUI_DESKTOP_WINDOW_DELIVER_INPUT 6U

typedef enum {
    GUI_WIN_EVT_NONE = 0,
    GUI_WIN_EVT_PAINT,
    GUI_WIN_EVT_FOCUS_GAINED,
    GUI_WIN_EVT_FOCUS_LOST,
    GUI_WIN_EVT_MOUSE_DOWN,
    GUI_WIN_EVT_MOUSE_UP,
    GUI_WIN_EVT_MOUSE_MOVE,
    GUI_WIN_EVT_MOUSE_WHEEL,
    GUI_WIN_EVT_KEY_DOWN,
    GUI_WIN_EVT_KEY_UP,
    GUI_WIN_EVT_CHAR,
    GUI_WIN_EVT_WINDOW_RESIZED,
    GUI_WIN_EVT_CLOSE_REQUEST,
    GUI_WIN_EVT_DESKTOP_OPEN_PATH
} gui_window_event_type_t;

typedef struct {
    uint16_t type;
    int16_t arg0;
    int16_t arg1;
    int32_t arg2;
} gui_window_event_t;

typedef struct {
    uint32_t size;
    uint32_t flags;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} gui_create_window_params_t;

typedef struct {
    uint32_t size;
    uint32_t flags;
    int32_t window_x;
    int32_t window_y;
    int32_t window_width;
    int32_t window_height;
    int32_t client_x;
    int32_t client_y;
    int32_t client_width;
    int32_t client_height;
} gui_window_info_t;

typedef struct {
    uint32_t size;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t format;
} gui_screen_info_t;

typedef struct {
    uint32_t size;
    uint32_t flags;
    uintptr_t buffer_ptr;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t stride_bytes;
} gui_present_params_t;

typedef struct {
    int32_t window_id;
    uint32_t flags;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    char title[32];
} gui_window_snapshot_entry_t;

typedef struct {
    uint32_t size;
    int32_t window_id;
    uint32_t action;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    uint32_t event_type;
    int32_t event_arg0;
    int32_t event_arg1;
    int32_t event_arg2;
} gui_desktop_window_action_t;

typedef struct {
    uint32_t size;
    int32_t window_id;
    int32_t x;
    int32_t y;
    uint32_t surface_width;
    uint32_t surface_height;
    uintptr_t buffer_ptr;
    uint32_t buffer_bytes;
    uint32_t width;
    uint32_t height;
    uint32_t stride_bytes;
    uint32_t format;
    uint32_t flags;
} gui_window_surface_read_t;

#define GUI_WINDOW_SURFACE_FLAG_FULL_WINDOW 0x00000001U
#define GUI_WINDOW_SURFACE_FLAG_DIRTY_RECT  0x00000002U
#define GUI_WINDOW_SURFACE_FLAG_MORE_DIRTY_RECTS 0x00000004U

#endif
