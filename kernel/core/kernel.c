#include <stdint.h>
#include "string.h"
#include "fs.h"
#include "rtc.h"
#include "editor.h"
#include "memory_alloc.h"
#include "arch.h"
#include "paging.h"
#include "process.h"
#include "fd.h"
#include "cpu.h"
#include "pci.h"
#include "storage.h"
#include "vbe.h"
#include "mouse.h"
#include "serial.h"
#include "syscall.h"
#include "usermode.h"
#include "net.h"

extern void outb(uint16_t port, uint8_t val);
extern void outw(uint16_t port, uint16_t val);
extern uint8_t inb(uint16_t port);
extern void clear_screen();
extern void screen_set_graphics_enabled(int enabled);
extern int screen_is_graphics_enabled(void);
extern void vga_print(const char* str);
extern void vga_print_color(const char* str, uint8_t color);
extern void vga_println(const char* str);
extern void vga_print_int(int num);
extern void vga_scrollback_lines(int direction);
extern int keyboard_deliver_desktop_input(uint8_t scancode, int modifiers);
extern void init_keyboard();
extern disk_fs_node_t dir_cache[MAX_FILES];
extern int current_dir_index;

extern void print_memory_info();

#if UINTPTR_MAX > 0xFFFFFFFFU
extern void x64_expect_fault(uint64_t vector, uint64_t resume_rip, uint64_t fault_addr, int test_kind);
extern int x64_last_fault_test_passed(void);
extern void x64_test_page_fault(void);
extern void x64_test_page_fault_resume(void);
#endif

void vga_print_int_hex(uint32_t n, char* buf);

// Global variables for usermode jump
volatile uint32_t usermode_jump_eip;
volatile uint32_t usermode_jump_esp;

volatile uint32_t timer_ticks = 0;
static volatile int kernel_graphics_ready = 0;
static volatile int kernel_waitpid_test_release = 0;

typedef struct {
    volatile int writer_status;
    volatile int reader_status;
    volatile uint32_t bytes_written;
    volatile uint32_t bytes_read;
    uint32_t target_bytes;
    int read_fd;
    int write_fd;
} kernel_pipe_test_state_t;

static kernel_pipe_test_state_t kernel_pipe_test_state;

#define DESKTOP_OPEN_PATH_MAX 256U
#define NARCOS_BOOT_INFO_ADDR 0x7000U
#define NARCOS_BOOT_INFO_MAGIC 0x4243524EU
#define NARCOS_BOOT_INFO_VERSION_MAX 3U
#define NARCOS_BOOT_INFO_MIN_SIZE 32U
#define NARCOS_BOOT_FLAG_GRAPHICS 0x00000001U
#define NARCOS_BOOT_FLAG_SAFE_TEXT 0x00000002U
#define NARCOS_BOOT_FLAG_SERIAL 0x00000004U
#define NARCOS_BOOT_FLAG_DEBUG 0x00000008U
#define NARCOS_BOOT_FLAG_LONG_MODE 0x00000010U

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t flags;
    uint8_t boot_drive;
    uint8_t profile;
    uint16_t reserved;
    uint32_t kernel_lba;
    uint32_t kernel_sectors;
    uint16_t vbe_mode;
    uint16_t target_width;
    uint16_t target_height;
    uint16_t e820_count;
    uint32_t framebuffer_addr;
    uint32_t framebuffer_size;
    uint32_t kernel_load_addr;
    uint32_t kernel_load_size;
    uint16_t fb_width;
    uint16_t fb_height;
    uint16_t fb_pitch;
    uint8_t fb_bpp;
    uint8_t fb_memory_model;
    uint8_t red_mask;
    uint8_t red_position;
    uint8_t green_mask;
    uint8_t green_position;
    uint8_t blue_mask;
    uint8_t blue_position;
    uint8_t rsv_mask;
    uint8_t rsv_position;
    uint32_t e820_map_addr;
    uint16_t e820_entry_size;
    uint16_t boot_manifest_version;
    uint32_t rsdp_addr;
    uint32_t kernel_entry;
    uint32_t kernel_crc32;
    uint32_t initrd_lba;
    uint32_t initrd_sectors;
    uint32_t initrd_size;
    uint32_t initrd_crc32;
} __attribute__((packed)) narcos_boot_info_t;

window_t windows[MAX_WINDOWS];
int window_count = 0;
int active_window_idx = -1;
static int legacy_desktop_dir_index = -1;
static int next_user_window_id = 100;
static int desktop_owner_pid = 0;
static uint16_t desktop_event_head = 0;
static uint16_t desktop_event_tail = 0;
static gui_window_event_t desktop_event_queue[WINDOW_EVENT_QUEUE_CAP];
static char desktop_open_path[DESKTOP_OPEN_PATH_MAX];
static int desktop_open_path_pending = 0;
static int gui_dirty_valid = 0;
static int gui_dirty_x = 0;
static int gui_dirty_y = 0;
static int gui_dirty_w = 0;
static int gui_dirty_h = 0;
void nwm_bring_to_front(int idx);

char pad_title[32] = "NarcPad";
volatile int snk_next_dir = -1;

int snk_px[100], snk_py[100], snk_len = 5, apple_x = 10, apple_y = 10;
int snk_dead = 0, snk_score = 0, snk_best = 0;

enum {
    WINDOW_RESIZE_NONE = 0,
    WINDOW_RESIZE_LEFT = 1 << 0,
    WINDOW_RESIZE_RIGHT = 1 << 1,
    WINDOW_RESIZE_BOTTOM = 1 << 2
};

static void gui_mark_dirty_rect(int x, int y, int w, int h) {
    int screen_w = (int)vbe_get_width();
    int screen_h = (int)vbe_get_height();
    int x2;
    int y2;

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
    if (!gui_dirty_valid) {
        gui_dirty_valid = 1;
        gui_dirty_x = x;
        gui_dirty_y = y;
        gui_dirty_w = w;
        gui_dirty_h = h;
        return;
    }
    if (x < gui_dirty_x) gui_dirty_x = x;
    if (y < gui_dirty_y) gui_dirty_y = y;
    if (x2 > gui_dirty_x + gui_dirty_w) gui_dirty_w = x2 - gui_dirty_x;
    if (y2 > gui_dirty_y + gui_dirty_h) gui_dirty_h = y2 - gui_dirty_y;
}

static void gui_mark_dirty_full(void) {
    gui_mark_dirty_rect(0, 0, (int)vbe_get_width(), (int)vbe_get_height());
}

static void gui_mark_window_dirty(const window_t* win) {
    if (!win || !win->visible || win->minimized) return;
    gui_mark_dirty_rect(win->x, win->y, win->w, win->h);
}

static void merge_rect_bounds(int* x, int* y, int* w, int* h, int add_x, int add_y, int add_w, int add_h) {
    int x2;
    int y2;
    int add_x2;
    int add_y2;

    if (!x || !y || !w || !h || add_w <= 0 || add_h <= 0) return;
    if (*w <= 0 || *h <= 0) {
        *x = add_x;
        *y = add_y;
        *w = add_w;
        *h = add_h;
        return;
    }
    x2 = *x + *w;
    y2 = *y + *h;
    add_x2 = add_x + add_w;
    add_y2 = add_y + add_h;
    if (add_x < *x) *x = add_x;
    if (add_y < *y) *y = add_y;
    if (add_x2 > x2) x2 = add_x2;
    if (add_y2 > y2) y2 = add_y2;
    *w = x2 - *x;
    *h = y2 - *y;
}

static int rects_overlap_or_touch(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    return aw > 0 && ah > 0 && bw > 0 && bh > 0 &&
           ax <= bx + bw && ay <= by + bh &&
           bx <= ax + aw && by <= ay + ah;
}

static void nwm_mark_window_surface_damage(window_t* win, int x, int y, int w, int h) {
    if (!win || w <= 0 || h <= 0) return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if ((uint32_t)x >= win->client_surface_w || (uint32_t)y >= win->client_surface_h) return;
    if ((uint32_t)(x + w) > win->client_surface_w) w = (int)win->client_surface_w - x;
    if ((uint32_t)(y + h) > win->client_surface_h) h = (int)win->client_surface_h - y;
    if (w <= 0 || h <= 0) return;
    for (uint8_t i = 0; i < win->client_damage_count; i++) {
        if (rects_overlap_or_touch(win->client_damage_x[i], win->client_damage_y[i],
                                   win->client_damage_w[i], win->client_damage_h[i],
                                   x, y, w, h)) {
            merge_rect_bounds(&win->client_damage_x[i], &win->client_damage_y[i],
                              &win->client_damage_w[i], &win->client_damage_h[i], x, y, w, h);
            return;
        }
    }
    if (win->client_damage_count < WINDOW_SURFACE_DAMAGE_RECT_CAP) {
        uint8_t slot = win->client_damage_count++;

        win->client_damage_x[slot] = x;
        win->client_damage_y[slot] = y;
        win->client_damage_w[slot] = w;
        win->client_damage_h[slot] = h;
        return;
    }
    merge_rect_bounds(&win->client_damage_x[0], &win->client_damage_y[0],
                      &win->client_damage_w[0], &win->client_damage_h[0], x, y, w, h);
    for (uint8_t i = 1; i < win->client_damage_count; i++) {
        merge_rect_bounds(&win->client_damage_x[0], &win->client_damage_y[0],
                          &win->client_damage_w[0], &win->client_damage_h[0],
                          win->client_damage_x[i], win->client_damage_y[i],
                          win->client_damage_w[i], win->client_damage_h[i]);
    }
    win->client_damage_count = 1;
}

static void nwm_free_window_client_surface(window_t* win) {
    if (!win || !win->client_surface) return;
    if (win->client_surface_pages != 0U) {
        free_physical_pages(win->client_surface, win->client_surface_pages);
    } else {
        free(win->client_surface);
    }
    win->client_surface = 0;
    win->client_surface_pages = 0;
    win->client_surface_w = 0;
    win->client_surface_h = 0;
    win->client_surface_bpp = 0;
    win->client_damage_count = 0;
}

static int nwm_ensure_window_client_surface(window_t* win, int client_w, int client_h) {
    size_t surface_bytes;
    uint32_t surface_pages;

    if (!win || client_w <= 0 || client_h <= 0) return -1;
    if (win->client_surface != 0 &&
        win->client_surface_w == (uint32_t)client_w &&
        win->client_surface_h == (uint32_t)client_h &&
        win->client_surface_bpp == 4U) {
        return 0;
    }

    surface_bytes = (size_t)client_w * (size_t)client_h * 4U;
    surface_pages = (uint32_t)((surface_bytes + 4095U) / 4096U);
    nwm_free_window_client_surface(win);
    win->client_surface = (uint8_t*)alloc_physical_pages(surface_pages);
    if (!win->client_surface) {
        win->client_surface_w = 0;
        win->client_surface_h = 0;
        win->client_surface_bpp = 0;
        serial_write("[gui-present] fail alloc bytes=");
        serial_write_hex32((uint32_t)surface_bytes);
        serial_write_char('\n');
        return -1;
    }
    memset(win->client_surface, 0, (size_t)surface_pages * 4096U);
    win->client_surface_pages = surface_pages;
    win->client_surface_w = (uint32_t)client_w;
    win->client_surface_h = (uint32_t)client_h;
    win->client_surface_bpp = 4U;
    return 0;
}

static void nwm_consume_window_surface_damage(window_t* win) {
    if (!win || win->client_damage_count == 0U) return;
    for (uint8_t i = 1; i < win->client_damage_count; i++) {
        win->client_damage_x[i - 1] = win->client_damage_x[i];
        win->client_damage_y[i - 1] = win->client_damage_y[i];
        win->client_damage_w[i - 1] = win->client_damage_w[i];
        win->client_damage_h[i - 1] = win->client_damage_h[i];
    }
    win->client_damage_count--;
}

static int rect_contains_rect(int outer_x, int outer_y, int outer_w, int outer_h,
                              int inner_x, int inner_y, int inner_w, int inner_h) {
    return inner_w >= 0 && inner_h >= 0 &&
           inner_x >= outer_x &&
           inner_y >= outer_y &&
           inner_x + inner_w <= outer_x + outer_w &&
           inner_y + inner_h <= outer_y + outer_h;
}

static int nwm_window_min_w(window_type_t type) {
    switch (type) {
        case WIN_TYPE_TERMINAL: return 460;
        default: return 260;
    }
}

static int nwm_window_min_h(window_type_t type) {
    switch (type) {
        case WIN_TYPE_TERMINAL: return 300;
        default: return 180;
    }
}

static int nwm_window_default_w(window_type_t type, int screen_w) {
    int min_w = nwm_window_min_w(type);
    int max_w = screen_w - 72;
    int target;

    if (max_w < min_w) max_w = min_w;
    switch (type) {
        case WIN_TYPE_TERMINAL: target = (screen_w * 58) / 100; break;
        default: target = min_w; break;
    }
    if (target < min_w) target = min_w;
    if (target > max_w) target = max_w;
    return target;
}

static int nwm_window_default_h(window_type_t type, int screen_h) {
    int min_h = nwm_window_min_h(type);
    int max_h = screen_h - 96;
    int target;

    if (max_h < min_h) max_h = min_h;
    switch (type) {
        case WIN_TYPE_TERMINAL: target = (screen_h * 62) / 100; break;
        default: target = min_h; break;
    }
    if (target < min_h) target = min_h;
    if (target > max_h) target = max_h;
    return target;
}

static void nwm_fit_window(window_t* win, int recenter) {
    int min_w;
    int min_h;
    int sw;
    int sh;
    int max_w;
    int max_h;

    if (!win) return;
    sw = (int)vbe_get_width();
    sh = (int)vbe_get_height();
    if (sw <= 0 || sh <= 0) return;

    min_w = nwm_window_min_w(win->type);
    min_h = nwm_window_min_h(win->type);
    max_w = sw - 48;
    max_h = sh - 72;
    if (max_w < min_w) max_w = min_w;
    if (max_h < min_h) max_h = min_h;

    if (win->w < min_w) win->w = min_w;
    if (win->h < min_h) win->h = min_h;
    if (win->w > max_w) win->w = max_w;
    if (win->h > max_h) win->h = max_h;

    if (recenter) {
        win->x = (sw - win->w) / 2;
        win->y = 42 + ((sh - 74 - win->h) / 2);
    }

    if (win->x < 12) win->x = 12;
    if (win->y < 35) win->y = 35;
    if (win->x + win->w > sw - 12) win->x = sw - 12 - win->w;
    if (win->y + win->h > sh - 12) win->y = sh - 12 - win->h;
    if (win->x < 12) win->x = 12;
    if (win->y < 35) win->y = 35;
}

static int nwm_get_idx_by_id(int id) {
    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == id) return i;
    }
    return -1;
}

static int nwm_window_is_borderless(const window_t* win) {
    return win && (win->flags & GUI_WINDOW_FLAG_BORDERLESS) != 0U;
}

static int nwm_window_is_fullscreen(const window_t* win) {
    return win && (win->flags & GUI_WINDOW_FLAG_FULLSCREEN) != 0U;
}

static int nwm_window_is_desktop_surface(const window_t* win) {
    return win && win->type == WIN_TYPE_USER &&
           win->owner_pid == desktop_owner_pid &&
           nwm_window_is_borderless(win) &&
           nwm_window_is_fullscreen(win);
}

static int nwm_desktop_surface_active(void) {
    for (int i = 0; i < window_count; i++) {
        if (!windows[i].visible || windows[i].minimized) continue;
        if (!nwm_window_is_desktop_surface(&windows[i])) continue;
        if (!windows[i].client_surface ||
            windows[i].client_surface_w == 0U ||
            windows[i].client_surface_h == 0U) {
            continue;
        }
        return 1;
    }
    return 0;
}

static void desktop_watchdog_tick(void) {
    static uint32_t owner_without_surface_since = 0;
    int owner_pid;
    int surface_active;

    if (!screen_is_graphics_enabled()) return;

    owner_pid = nwm_get_desktop_owner_pid();
    surface_active = nwm_desktop_surface_active();

    if (owner_pid > 0 && !surface_active) {
        if (owner_without_surface_since == 0U) {
            owner_without_surface_since = timer_ticks;
        } else if (timer_ticks - owner_without_surface_since >= 300U) {
            process_debug_dump("desktop-watchdog");
            owner_without_surface_since = timer_ticks;
        }
    } else {
        owner_without_surface_since = 0U;
    }
}

static int nwm_dispatch_kernel_window_input(int idx, uint32_t event_type, int arg0, int arg1, int arg2);

static int nwm_pick_active_window_idx(void) {
    int desktop_idx = -1;

    for (int i = window_count - 1; i >= 0; i--) {
        if (!windows[i].visible || windows[i].minimized) continue;
        if (nwm_window_is_desktop_surface(&windows[i])) {
            if (desktop_idx == -1) desktop_idx = i;
            continue;
        }
        return i;
    }
    return desktop_idx;
}

static void nwm_send_to_back(int idx) {
    window_t tmp;

    if (idx <= 0 || idx >= window_count) return;
    tmp = windows[idx];
    for (int i = idx; i > 0; i--) {
        windows[i] = windows[i - 1];
    }
    windows[0] = tmp;
}

static int nwm_queue_window_event_idx(int idx, uint16_t type, int16_t arg0, int16_t arg1, int32_t arg2) {
    uint16_t next_tail;

    if (idx < 0 || idx >= window_count) return -1;
    next_tail = (uint16_t)((windows[idx].event_tail + 1U) % WINDOW_EVENT_QUEUE_CAP);
    if (next_tail == windows[idx].event_head) {
        windows[idx].event_head = (uint16_t)((windows[idx].event_head + 1U) % WINDOW_EVENT_QUEUE_CAP);
    }
    windows[idx].event_queue[windows[idx].event_tail].type = type;
    windows[idx].event_queue[windows[idx].event_tail].arg0 = arg0;
    windows[idx].event_queue[windows[idx].event_tail].arg1 = arg1;
    windows[idx].event_queue[windows[idx].event_tail].arg2 = arg2;
    windows[idx].event_tail = next_tail;
    return 0;
}

static int nwm_pop_window_event_idx(int idx, gui_window_event_t* out_event) {
    if (idx < 0 || idx >= window_count || !out_event) return -1;
    if (windows[idx].event_head == windows[idx].event_tail) return 0;
    *out_event = windows[idx].event_queue[windows[idx].event_head];
    windows[idx].event_head = (uint16_t)((windows[idx].event_head + 1U) % WINDOW_EVENT_QUEUE_CAP);
    return 1;
}

static int nwm_queue_desktop_event_internal(uint16_t type, int16_t arg0, int16_t arg1, int32_t arg2) {
    uint16_t next_tail;

    if (desktop_owner_pid <= 0) return -1;
    if (type == GUI_WIN_EVT_PAINT) {
        uint16_t scan = desktop_event_head;

        while (scan != desktop_event_tail) {
            if (desktop_event_queue[scan].type == GUI_WIN_EVT_PAINT &&
                (desktop_event_queue[scan].arg2 == 0 || desktop_event_queue[scan].arg2 == arg2)) {
                return 0;
            }
            scan = (uint16_t)((scan + 1U) % WINDOW_EVENT_QUEUE_CAP);
        }
    }
    next_tail = (uint16_t)((desktop_event_tail + 1U) % WINDOW_EVENT_QUEUE_CAP);
    if (next_tail == desktop_event_head) {
        desktop_event_head = (uint16_t)((desktop_event_head + 1U) % WINDOW_EVENT_QUEUE_CAP);
    }
    desktop_event_queue[desktop_event_tail].type = type;
    desktop_event_queue[desktop_event_tail].arg0 = arg0;
    desktop_event_queue[desktop_event_tail].arg1 = arg1;
    desktop_event_queue[desktop_event_tail].arg2 = arg2;
    desktop_event_tail = next_tail;
    return 0;
}

void nwm_queue_desktop_event(uint16_t type, int16_t arg0, int16_t arg1, int32_t arg2) {
    (void)nwm_queue_desktop_event_internal(type, arg0, arg1, arg2);
}

int nwm_poll_desktop_event(int owner_pid, gui_window_event_t* out_event) {
    if (owner_pid <= 0 || owner_pid != desktop_owner_pid || !out_event) return -1;
    if (desktop_event_head == desktop_event_tail) return 0;
    *out_event = desktop_event_queue[desktop_event_head];
    desktop_event_head = (uint16_t)((desktop_event_head + 1U) % WINDOW_EVENT_QUEUE_CAP);
    return 1;
}

int nwm_list_windows_for_desktop(int owner_pid, gui_window_snapshot_entry_t* out_entries, int max_entries) {
    int count = 0;

    if (owner_pid <= 0 || owner_pid != desktop_owner_pid || !out_entries || max_entries <= 0) return -1;
    for (int i = 0; i < window_count && count < max_entries; i++) {
        uint32_t flags = 0U;

        if (!windows[i].visible) continue;
        if (nwm_window_is_desktop_surface(&windows[i])) continue;
        if (i == active_window_idx) flags |= GUI_WINDOW_SNAPSHOT_ACTIVE;
        if (windows[i].visible) flags |= GUI_WINDOW_SNAPSHOT_VISIBLE;
        if (windows[i].minimized) flags |= GUI_WINDOW_SNAPSHOT_MINIMIZED;
        if (nwm_window_is_borderless(&windows[i])) flags |= GUI_WINDOW_SNAPSHOT_BORDERLESS;
        if (nwm_window_is_fullscreen(&windows[i])) flags |= GUI_WINDOW_SNAPSHOT_FULLSCREEN;
        if (windows[i].type == WIN_TYPE_USER) flags |= GUI_WINDOW_SNAPSHOT_USER;
        out_entries[count].window_id = windows[i].id;
        out_entries[count].flags = flags;
        out_entries[count].x = windows[i].x;
        out_entries[count].y = windows[i].y;
        out_entries[count].width = windows[i].w;
        out_entries[count].height = windows[i].h;
        strncpy(out_entries[count].title, windows[i].title, sizeof(out_entries[count].title) - 1U);
        out_entries[count].title[sizeof(out_entries[count].title) - 1U] = '\0';
        count++;
    }
    return count;
}

int nwm_read_window_surface_for_desktop(int owner_pid, gui_window_surface_read_t* io) {
    int idx;
    int read_x = 0;
    int read_y = 0;
    int read_w = 0;
    int read_h = 0;
    uint8_t* surface = 0;
    uint32_t surface_w = 0;
    uint32_t surface_h = 0;
    uint32_t source_bpp_bytes = 4U;
    uint32_t row_bytes;
    uint32_t stride_bytes;
    size_t required_bytes;
    int use_damage_rect = 0;
    int damage_contains_read = 0;

    if (owner_pid <= 0 || owner_pid != desktop_owner_pid || !io) return -1;
    idx = nwm_get_idx_by_id(io->window_id);
    if (idx < 0) return -1;
    if (nwm_window_is_desktop_surface(&windows[idx])) return -1;

    if (windows[idx].type != WIN_TYPE_TERMINAL &&
        windows[idx].client_surface &&
        windows[idx].client_surface_w != 0U &&
        windows[idx].client_surface_h != 0U) {
        surface = windows[idx].client_surface;
        surface_w = windows[idx].client_surface_w;
        surface_h = windows[idx].client_surface_h;
        source_bpp_bytes = 4U;
        io->flags = 0U;
    } else if (windows[idx].type == WIN_TYPE_TERMINAL) {
        extern int vga_get_window_w(void);
        extern int vga_get_window_h(void);
        extern int vga_window_needs_refresh(void);
        extern void vga_refresh_window(void);
        extern void* vga_get_window_buffer(void);

        if (vga_window_needs_refresh()) vga_refresh_window();
        surface = (uint8_t*)vga_get_window_buffer();
        surface_w = (uint32_t)vga_get_window_w();
        surface_h = (uint32_t)vga_get_window_h();
        if (!surface || surface_w == 0U || surface_h == 0U) return -1;
        source_bpp_bytes = vbe_get_bpp() / 8U;
        if (source_bpp_bytes == 0U) return -1;
        io->flags = GUI_WINDOW_SURFACE_FLAG_FULL_WINDOW;
    } else {
        return -1;
    }

    row_bytes = surface_w * 4U;
    io->surface_width = surface_w;
    io->surface_height = surface_h;
    if (io->width == 0U && io->height == 0U) {
        read_w = (int)surface_w;
        read_h = (int)surface_h;
        if (windows[idx].type == WIN_TYPE_USER && windows[idx].client_damage_count != 0U) {
            read_x = windows[idx].client_damage_x[0];
            read_y = windows[idx].client_damage_y[0];
            read_w = windows[idx].client_damage_w[0];
            read_h = windows[idx].client_damage_h[0];
            io->flags |= GUI_WINDOW_SURFACE_FLAG_DIRTY_RECT;
            if (windows[idx].client_damage_count > 1U) io->flags |= GUI_WINDOW_SURFACE_FLAG_MORE_DIRTY_RECTS;
            use_damage_rect = 1;
        }
    } else if (io->width == 0U || io->height == 0U) {
        return -1;
    } else {
        read_x = io->x;
        read_y = io->y;
        read_w = (int)io->width;
        read_h = (int)io->height;
    }
    if (read_x < 0 || read_y < 0 || read_w <= 0 || read_h <= 0) return -1;
    if ((uint32_t)read_x > surface_w || (uint32_t)read_y > surface_h) return -1;
    if ((uint32_t)read_w > surface_w - (uint32_t)read_x ||
        (uint32_t)read_h > surface_h - (uint32_t)read_y) return -1;

    row_bytes = (uint32_t)read_w * 4U;
    stride_bytes = io->stride_bytes != 0U ? io->stride_bytes : row_bytes;
    if (stride_bytes < row_bytes) return -1;
    required_bytes = read_h > 0 ? (size_t)stride_bytes * (size_t)(read_h - 1) + (size_t)row_bytes : 0U;
    io->x = read_x;
    io->y = read_y;
    io->width = (uint32_t)read_w;
    io->height = (uint32_t)read_h;
    io->stride_bytes = stride_bytes;
    io->format = GUI_PIXEL_FORMAT_XRGB8888;
    if (windows[idx].type == WIN_TYPE_USER && windows[idx].client_damage_count != 0U) {
        damage_contains_read = rect_contains_rect(read_x, read_y, read_w, read_h,
                                                  windows[idx].client_damage_x[0], windows[idx].client_damage_y[0],
                                                  windows[idx].client_damage_w[0], windows[idx].client_damage_h[0]);
    }

    if (io->buffer_ptr == 0U || io->buffer_bytes == 0U) return 0;
    if ((size_t)io->buffer_bytes < required_bytes) return -1;
    if (source_bpp_bytes == 4U) {
        for (int y = 0; y < read_h; y++) {
            const uint8_t* src_row = surface +
                                     (size_t)(read_y + y) * (size_t)surface_w * 4U +
                                     (size_t)read_x * 4U;
            void* dst_row = (void*)(io->buffer_ptr + (uintptr_t)y * (uintptr_t)stride_bytes);

            if (copy_to_user(dst_row, src_row, row_bytes) != 0) return -1;
        }
    } else {
        uint32_t row_buffer[2048];

        if ((uint32_t)read_w > (uint32_t)(sizeof(row_buffer) / sizeof(row_buffer[0]))) return -1;
        for (int y = 0; y < read_h; y++) {
            uint8_t* src_row = surface +
                               (size_t)(read_y + y) * (size_t)surface_w * (size_t)source_bpp_bytes +
                               (size_t)read_x * (size_t)source_bpp_bytes;

            for (int x = 0; x < read_w; x++) {
                if (source_bpp_bytes == 3U) {
                    uint8_t* px = src_row + (size_t)x * 3U;
                    row_buffer[x] = ((uint32_t)px[2] << 16) | ((uint32_t)px[1] << 8) | (uint32_t)px[0];
                } else if (source_bpp_bytes == 2U) {
                    uint16_t raw = *(uint16_t*)(src_row + (size_t)x * 2U);
                    uint32_t r = ((raw >> 11) & 0x1FU) << 3;
                    uint32_t g = ((raw >> 5) & 0x3FU) << 2;
                    uint32_t b = (raw & 0x1FU) << 3;
                    row_buffer[x] = (r << 16) | (g << 8) | b;
                } else {
                    return -1;
                }
            }
            if (copy_to_user((void*)(io->buffer_ptr + (uintptr_t)y * (uintptr_t)stride_bytes),
                             row_buffer, row_bytes) != 0) {
                return -1;
            }
        }
    }
    if (windows[idx].type == WIN_TYPE_USER && windows[idx].client_damage_count != 0U &&
        (use_damage_rect || damage_contains_read)) {
        nwm_consume_window_surface_damage(&windows[idx]);
        if (windows[idx].client_damage_count > 0U) io->flags |= GUI_WINDOW_SURFACE_FLAG_MORE_DIRTY_RECTS;
    }
    return 0;
}

int nwm_desktop_window_action(int owner_pid, const gui_desktop_window_action_t* action) {
    int idx;
    int old_w;
    int old_h;

    if (owner_pid <= 0 || owner_pid != desktop_owner_pid || !action) return -1;
    idx = nwm_get_idx_by_id(action->window_id);
    if (idx < 0) return -1;
    if (nwm_window_is_desktop_surface(&windows[idx])) return -1;

    switch (action->action) {
        case GUI_DESKTOP_WINDOW_FOCUS:
            if (windows[idx].type != WIN_TYPE_USER) windows[idx].visible = 1;
            if (windows[idx].minimized) windows[idx].minimized = 0;
            nwm_bring_to_front(idx);
            gui_mark_dirty_full();
            gui_needs_redraw = 1;
            return 0;
        case GUI_DESKTOP_WINDOW_MINIMIZE:
            gui_mark_window_dirty(&windows[idx]);
            windows[idx].minimized = 1;
            active_window_idx = nwm_pick_active_window_idx();
            gui_mark_dirty_full();
            gui_needs_redraw = 1;
            return 0;
        case GUI_DESKTOP_WINDOW_RESTORE:
            if (windows[idx].type != WIN_TYPE_USER) windows[idx].visible = 1;
            windows[idx].minimized = 0;
            nwm_bring_to_front(idx);
            gui_mark_dirty_full();
            gui_needs_redraw = 1;
            return 0;
        case GUI_DESKTOP_WINDOW_SET_RECT:
            if (nwm_window_is_borderless(&windows[idx]) || nwm_window_is_fullscreen(&windows[idx])) return -1;
            old_w = windows[idx].w;
            old_h = windows[idx].h;
            gui_mark_window_dirty(&windows[idx]);
            windows[idx].x = action->x;
            windows[idx].y = action->y;
            windows[idx].w = action->width;
            windows[idx].h = action->height;
            nwm_fit_window(&windows[idx], 0);
            gui_mark_window_dirty(&windows[idx]);
            if (windows[idx].w != old_w || windows[idx].h != old_h) {
                if (windows[idx].type == WIN_TYPE_USER) {
                    (void)nwm_queue_window_event_idx(idx, GUI_WIN_EVT_WINDOW_RESIZED,
                                                     (int16_t)windows[idx].w, (int16_t)windows[idx].h, 0);
                    (void)nwm_queue_window_event_idx(idx, GUI_WIN_EVT_PAINT, 0, 0, 0);
                }
            }
            gui_needs_redraw = 1;
            return 0;
        case GUI_DESKTOP_WINDOW_CLOSE_REQUEST:
            gui_mark_window_dirty(&windows[idx]);
            if (windows[idx].type == WIN_TYPE_USER) {
                (void)nwm_queue_window_event_idx(idx, GUI_WIN_EVT_CLOSE_REQUEST, 0, 0, 0);
            } else {
                windows[idx].visible = 0;
                active_window_idx = nwm_pick_active_window_idx();
            }
            gui_needs_redraw = 1;
            return 0;
        case GUI_DESKTOP_WINDOW_DELIVER_INPUT:
            if (windows[idx].minimized) return -1;
            if (windows[idx].type == WIN_TYPE_USER) {
                switch (action->event_type) {
                    case GUI_WIN_EVT_MOUSE_DOWN:
                    case GUI_WIN_EVT_MOUSE_UP:
                    case GUI_WIN_EVT_MOUSE_MOVE:
                    case GUI_WIN_EVT_MOUSE_WHEEL:
                    case GUI_WIN_EVT_KEY_DOWN:
                    case GUI_WIN_EVT_KEY_UP:
                    case GUI_WIN_EVT_CHAR:
                        return nwm_queue_window_event_idx(idx, (uint16_t)action->event_type,
                                                          (int16_t)action->event_arg0,
                                                          (int16_t)action->event_arg1,
                                                          action->event_arg2);
                    default:
                        return -1;
                }
            }
            return nwm_dispatch_kernel_window_input(idx, action->event_type,
                                                    action->event_arg0, action->event_arg1, action->event_arg2);
        default:
            return -1;
    }
}

void nwm_init_windows() {
    int sw = (int)vbe_get_width();
    int sh = (int)vbe_get_height();

    memset(windows, 0, sizeof(windows));
    active_window_idx = -1;

    windows[0].type = WIN_TYPE_TERMINAL;
    windows[0].w = nwm_window_default_w(WIN_TYPE_TERMINAL, sw);
    windows[0].h = nwm_window_default_h(WIN_TYPE_TERMINAL, sh);
    strcpy(windows[0].title, "Terminal");
    windows[0].visible = 0;
    windows[0].minimized = 0;
    windows[0].id = 0;
    windows[0].client_surface = 0;
    windows[0].client_surface_w = 0;
    windows[0].client_surface_h = 0;
    windows[0].client_surface_bpp = 0;
    windows[0].x = 28;
    windows[0].y = 44;
    nwm_fit_window(&windows[0], 0);
    window_count = 1;
}

void nwm_bring_to_front(int idx) {
    int old_active_id = -1;
    int old_active_idx = active_window_idx;

    if (idx < 0 || idx >= window_count) return;
    if (nwm_window_is_desktop_surface(&windows[idx])) {
        active_window_idx = nwm_pick_active_window_idx();
        return;
    }
    if (old_active_idx >= 0 && old_active_idx < window_count) old_active_id = windows[old_active_idx].id;
    window_t tmp = windows[idx];
    for (int i = idx; i < window_count - 1; i++) {
        windows[i] = windows[i+1];
    }
    windows[window_count - 1] = tmp;
    active_window_idx = window_count - 1;
    if (old_active_id != -1 && old_active_id != windows[active_window_idx].id) {
        int old_idx = nwm_get_idx_by_id(old_active_id);
        if (old_idx != -1) {
            (void)nwm_queue_window_event_idx(old_idx, GUI_WIN_EVT_FOCUS_LOST, 0, 0, 0);
            (void)nwm_queue_window_event_idx(old_idx, GUI_WIN_EVT_PAINT, 0, 0, 0);
        }
        (void)nwm_queue_window_event_idx(active_window_idx, GUI_WIN_EVT_FOCUS_GAINED, 0, 0, 0);
    }
    (void)nwm_queue_window_event_idx(active_window_idx, GUI_WIN_EVT_PAINT, 0, 0, 0);
    gui_needs_redraw = 1;
}

int nwm_get_idx_by_type(window_type_t type) {
    for (int i = 0; i < window_count; i++) {
        if (windows[i].type == type) return i;
    }
    return -1;
}

int nwm_find_window_at(int mx, int my) {
    for (int i = window_count - 1; i >= 0; i--) {
        if (!windows[i].visible || windows[i].minimized) continue;
        if (mx >= windows[i].x && mx <= windows[i].x + windows[i].w &&
            my >= windows[i].y && my <= windows[i].y + windows[i].h) {
            return i;
        }
    }
    return -1;
}

int nwm_create_user_window(int owner_pid, const gui_create_window_params_t* params) {
    window_t* win;
    int new_idx;
    int window_id;

    if (!params) {
        serial_write_line("[gui-create] fail params=null");
        return -1;
    }
    if (owner_pid <= 0) {
        serial_write_line("[gui-create] fail owner<=0");
        return -1;
    }
    if (window_count >= MAX_WINDOWS) {
        serial_write_line("[gui-create] fail window table full");
        return -1;
    }
    if ((params->flags & (GUI_WINDOW_FLAG_BORDERLESS | GUI_WINDOW_FLAG_FULLSCREEN)) ==
        (GUI_WINDOW_FLAG_BORDERLESS | GUI_WINDOW_FLAG_FULLSCREEN) &&
        owner_pid != desktop_owner_pid) {
        serial_write("[gui-create] fail desktop owner mismatch owner=");
        serial_write_hex32((uint32_t)owner_pid);
        serial_write(" desktop_owner=");
        serial_write_hex32((uint32_t)desktop_owner_pid);
        serial_write_char('\n');
        return -1;
    }
    win = &windows[window_count];
    memset(win, 0, sizeof(*win));
    win->type = WIN_TYPE_USER;
    win->x = params->x;
    win->y = params->y;
    win->w = params->width > 0 ? params->width : 420;
    win->h = params->height > 0 ? params->height : 280;
    win->visible = 1;
    win->minimized = 0;
    win->owner_pid = owner_pid;
    win->flags = params->flags;
    win->id = next_user_window_id++;
    window_id = win->id;
    strcpy(win->title, "User Window");
    if (nwm_window_is_fullscreen(win)) {
        win->x = 0;
        win->y = 0;
        win->w = (int)vbe_get_width();
        win->h = (int)vbe_get_height();
    } else {
        nwm_fit_window(win, params->x < 0 || params->y < 0);
    }
    window_count++;
    new_idx = window_count - 1;
    if (nwm_window_is_desktop_surface(&windows[new_idx])) {
        (void)nwm_queue_window_event_idx(new_idx, GUI_WIN_EVT_PAINT, 0, 0, 0);
        nwm_send_to_back(new_idx);
        active_window_idx = nwm_pick_active_window_idx();
        gui_needs_redraw = 1;
    } else {
        nwm_bring_to_front(new_idx);
    }
    return window_id;
}

int nwm_destroy_user_window(int owner_pid, int window_id) {
    int idx = nwm_get_idx_by_id(window_id);
    int was_active;

    if (idx == -1 || windows[idx].type != WIN_TYPE_USER || windows[idx].owner_pid != owner_pid) return -1;
    nwm_free_window_client_surface(&windows[idx]);
    was_active = idx == active_window_idx;
    for (int i = idx; i < window_count - 1; i++) {
        windows[i] = windows[i + 1];
    }
    window_count--;
    if (window_count < 0) window_count = 0;
    if (was_active) {
        active_window_idx = nwm_pick_active_window_idx();
    } else if (active_window_idx > idx) {
        active_window_idx--;
    } else if (active_window_idx >= window_count) {
        active_window_idx = nwm_pick_active_window_idx();
    }
    gui_needs_redraw = 1;
    return 0;
}

int nwm_set_user_window_title(int owner_pid, int window_id, const char* title) {
    int idx = nwm_get_idx_by_id(window_id);

    if (idx == -1 || windows[idx].type != WIN_TYPE_USER || windows[idx].owner_pid != owner_pid || !title) return -1;
    strncpy(windows[idx].title, title, sizeof(windows[idx].title) - 1U);
    windows[idx].title[sizeof(windows[idx].title) - 1U] = '\0';
    gui_mark_window_dirty(&windows[idx]);
    gui_needs_redraw = 1;
    return 0;
}

static void nwm_stretch_xrgb8888_row(uint32_t* row, uint32_t src_w, uint32_t dst_w) {
    uint32_t x_step;
    uint32_t x_acc;

    if (!row || src_w == 0U || dst_w <= src_w) return;
    x_step = (src_w << 16) / dst_w;
    if (x_step == 0U) x_step = 1U;
    x_acc = x_step * (dst_w - 1U);
    for (uint32_t dx = dst_w; dx > 0U; dx--) {
        uint32_t x = dx - 1U;
        uint32_t sx = x_acc >> 16;

        if (sx >= src_w) sx = src_w - 1U;
        row[x] = row[sx];
        x_acc -= x_step;
    }
}

static int nwm_present_user_window_stretched(window_t* win, const gui_present_params_t* params,
                                            int client_w, int client_h) {
    uint32_t src_w = params->width;
    uint32_t src_h = params->height;
    uint32_t src_row_bytes;
    uint32_t dst_row_pixels = (uint32_t)client_w;
    uint32_t y_step;
    uint32_t y_acc = 0;
    int prev_sy = -1;
    uint32_t* prev_dst = 0;

    if (!win || !params || client_w <= 0 || client_h <= 0) return -1;
    if (params->x != 0 || params->y != 0) return -1;
    if (src_w == 0U || src_h == 0U || src_w > (uint32_t)client_w || src_h > (uint32_t)client_h) return -1;
    src_row_bytes = src_w * 4U;
    if (params->stride_bytes < src_row_bytes) return -1;
    if (nwm_ensure_window_client_surface(win, client_w, client_h) != 0) return -1;

    y_step = (src_h << 16) / (uint32_t)client_h;
    for (int y = 0; y < client_h; y++) {
        int sy = (int)(y_acc >> 16);
        uint32_t* dst = (uint32_t*)(win->client_surface + (size_t)y * (size_t)dst_row_pixels * 4U);

        if ((uint32_t)sy >= src_h) sy = (int)src_h - 1;
        if (sy == prev_sy && prev_dst) {
            memcpy(dst, prev_dst, (size_t)dst_row_pixels * 4U);
        } else {
            if (copy_from_user(dst, (const void*)(uintptr_t)(params->buffer_ptr + (uintptr_t)sy * params->stride_bytes),
                               src_row_bytes) != 0) {
                serial_write("[gui-present] fail stretch row=");
                serial_write_hex32((uint32_t)sy);
                serial_write_char('\n');
                return -1;
            }
            nwm_stretch_xrgb8888_row(dst, src_w, dst_row_pixels);
            prev_sy = sy;
            prev_dst = dst;
        }
        y_acc += y_step;
    }
    return 0;
}

int nwm_present_user_window(int owner_pid, int window_id, const gui_present_params_t* params) {
    int idx = nwm_get_idx_by_id(window_id);
    int client_x;
    int client_y;
    int client_w;
    int client_h;
    int present_x;
    int present_y;
    uint8_t* surface;
    uint32_t row_bytes;
    uint32_t surface_row_bytes;
    int stretch_client;

    if (idx == -1 || !params) {
        serial_write_line("[gui-present] fail invalid-window-or-params");
        return -1;
    }
    if (windows[idx].type != WIN_TYPE_USER || windows[idx].owner_pid != owner_pid) {
        serial_write_line("[gui-present] fail owner-or-type");
        return -1;
    }
    vbe_get_window_client_rect(&windows[idx], &client_x, &client_y, &client_w, &client_h);
    if (params->width == 0U || params->height == 0U || params->buffer_ptr == 0U) {
        serial_write_line("[gui-present] fail empty-buffer");
        return -1;
    }
    if ((params->flags & ~GUI_PRESENT_FLAG_STRETCH_CLIENT) != 0U) {
        serial_write_line("[gui-present] fail flags");
        return -1;
    }
    present_x = params->x;
    present_y = params->y;
    stretch_client = (params->flags & GUI_PRESENT_FLAG_STRETCH_CLIENT) != 0U;
    row_bytes = params->width * 4U;
    if (params->stride_bytes < row_bytes) {
        serial_write_line("[gui-present] fail stride");
        return -1;
    }
    if (stretch_client) {
        if (nwm_present_user_window_stretched(&windows[idx], params, client_w, client_h) != 0) return -1;
        nwm_mark_window_surface_damage(&windows[idx], 0, 0, client_w, client_h);
        if (!nwm_window_is_desktop_surface(&windows[idx])) {
            (void)nwm_queue_desktop_event_internal(GUI_WIN_EVT_PAINT, 0, 0, window_id);
        }
        gui_mark_dirty_rect(client_x, client_y, client_w, client_h);
        gui_needs_redraw = 1;
        return 0;
    }
    if (present_x < 0 || present_y < 0) {
        serial_write_line("[gui-present] fail negative-origin");
        return -1;
    }
    if ((uint32_t)present_x > (uint32_t)client_w || (uint32_t)present_y > (uint32_t)client_h) {
        serial_write_line("[gui-present] fail origin-outside-client");
        return -1;
    }
    if (params->width > (uint32_t)(client_w - present_x) ||
        params->height > (uint32_t)(client_h - present_y)) {
        serial_write("[gui-present] fail size window=");
        serial_write_hex32((uint32_t)window_id);
        serial_write(" client=");
        serial_write_hex32((uint32_t)client_w);
        serial_write("x");
        serial_write_hex32((uint32_t)client_h);
        serial_write(" present=");
        serial_write_hex32(params->width);
        serial_write("x");
        serial_write_hex32(params->height);
        serial_write_char('\n');
        return -1;
    }

    if (nwm_ensure_window_client_surface(&windows[idx], client_w, client_h) != 0) return -1;

    surface = windows[idx].client_surface;
    surface_row_bytes = windows[idx].client_surface_w * 4U;
    for (uint32_t row = 0; row < params->height; row++) {
        uint8_t* dest_row = surface +
                            (size_t)(present_y + (int32_t)row) * surface_row_bytes +
                            (size_t)present_x * 4U;
        if (copy_from_user(dest_row,
                           (const void*)(uintptr_t)(params->buffer_ptr + (uintptr_t)row * params->stride_bytes),
                           row_bytes) != 0) {
            serial_write("[gui-present] fail copy row=");
            serial_write_hex32(row);
            serial_write(" ptr=");
            serial_write_hex64((uint64_t)(params->buffer_ptr + (uintptr_t)row * params->stride_bytes));
            serial_write_char('\n');
            return -1;
        }
    }
    nwm_mark_window_surface_damage(&windows[idx], present_x, present_y, (int)params->width, (int)params->height);
    if (!nwm_window_is_desktop_surface(&windows[idx])) {
        (void)nwm_queue_desktop_event_internal(GUI_WIN_EVT_PAINT, 0, 0, window_id);
    }
    gui_mark_dirty_rect(client_x + present_x, client_y + present_y, (int)params->width, (int)params->height);
    gui_needs_redraw = 1;
    return 0;
}

int nwm_get_user_window_info(int owner_pid, int window_id, gui_window_info_t* out_info) {
    int idx = nwm_get_idx_by_id(window_id);

    if (idx == -1 || !out_info) return -1;
    if (windows[idx].type != WIN_TYPE_USER || windows[idx].owner_pid != owner_pid) return -1;
    memset(out_info, 0, sizeof(*out_info));
    out_info->size = sizeof(*out_info);
    out_info->window_x = windows[idx].x;
    out_info->window_y = windows[idx].y;
    out_info->window_width = windows[idx].w;
    out_info->window_height = windows[idx].h;
    vbe_get_window_client_rect(&windows[idx], &out_info->client_x, &out_info->client_y,
                               &out_info->client_width, &out_info->client_height);
    return 0;
}

int nwm_poll_user_window_event(int owner_pid, int window_id, gui_window_event_t* out_event) {
    int idx = nwm_get_idx_by_id(window_id);

    if (idx == -1 || !out_event) return -1;
    if (windows[idx].type != WIN_TYPE_USER || windows[idx].owner_pid != owner_pid) return -1;
    return nwm_pop_window_event_idx(idx, out_event);
}

int nwm_get_screen_info(gui_screen_info_t* out_info) {
    if (!out_info) return -1;
    memset(out_info, 0, sizeof(*out_info));
    out_info->size = sizeof(*out_info);
    out_info->width = vbe_get_width();
    out_info->height = vbe_get_height();
    out_info->bpp = vbe_get_bpp();
    out_info->format = GUI_PIXEL_FORMAT_XRGB8888;
    return 0;
}

int nwm_register_desktop_owner(int owner_pid) {
    if (owner_pid <= 0) return -1;
    if (desktop_owner_pid != 0 && desktop_owner_pid != owner_pid) return -1;
    desktop_owner_pid = owner_pid;
    desktop_event_head = 0;
    desktop_event_tail = 0;
    return 0;
}

void nwm_release_desktop_owner(int owner_pid) {
    if (owner_pid > 0 && desktop_owner_pid == owner_pid) {
        desktop_owner_pid = 0;
        desktop_event_head = 0;
        desktop_event_tail = 0;
    }
}

int nwm_get_desktop_owner_pid(void) {
    return desktop_owner_pid;
}

int nwm_desktop_events_pending(void) {
    return desktop_event_head != desktop_event_tail;
}

int nwm_desktop_surface_active_state(void) {
    return nwm_desktop_surface_active();
}

void nwm_close_windows_for_owner(int owner_pid) {
    for (int i = window_count - 1; i >= 0; i--) {
        if (windows[i].type == WIN_TYPE_USER && windows[i].owner_pid == owner_pid) {
            (void)nwm_destroy_user_window(owner_pid, windows[i].id);
        }
    }
}

void nwm_queue_active_window_key_event(int keycode, int modifiers) {
    if (active_window_idx < 0 || active_window_idx >= window_count) return;
    if (windows[active_window_idx].type != WIN_TYPE_USER) return;
    (void)nwm_queue_window_event_idx(active_window_idx, GUI_WIN_EVT_KEY_DOWN, (int16_t)keycode, 0, modifiers);
}

void nwm_queue_active_window_key_up_event(int keycode, int modifiers) {
    if (active_window_idx < 0 || active_window_idx >= window_count) return;
    if (windows[active_window_idx].type != WIN_TYPE_USER) return;
    (void)nwm_queue_window_event_idx(active_window_idx, GUI_WIN_EVT_KEY_UP, (int16_t)keycode, 0, modifiers);
}

void nwm_queue_active_window_char_event(char c) {
    if (active_window_idx < 0 || active_window_idx >= window_count) return;
    if (windows[active_window_idx].type != WIN_TYPE_USER) return;
    (void)nwm_queue_window_event_idx(active_window_idx, GUI_WIN_EVT_CHAR, (int16_t)c, 0, 0);
}

void nwm_queue_active_window_mouse_move(int mx, int my) {
    if (active_window_idx < 0 || active_window_idx >= window_count) return;
    if (windows[active_window_idx].type != WIN_TYPE_USER || windows[active_window_idx].minimized) return;
    {
        int client_x;
        int client_y;
        int client_w;
        int client_h;
        int local_x;
        int local_y;

        vbe_get_window_client_rect(&windows[active_window_idx], &client_x, &client_y, &client_w, &client_h);
        local_x = mx - client_x;
        local_y = my - client_y;
        if (local_x < 0 || local_y < 0 || local_x >= client_w || local_y >= client_h) return;
        (void)nwm_queue_window_event_idx(active_window_idx, GUI_WIN_EVT_MOUSE_MOVE,
                                         (int16_t)local_x, (int16_t)local_y, 0);
    }
}

static int nwm_dispatch_kernel_window_input(int idx, uint32_t event_type, int arg0, int arg1, int arg2) {
    if (idx < 0 || idx >= window_count) return -1;
    (void)arg1;

    if (windows[idx].type == WIN_TYPE_TERMINAL) {
        if (event_type == GUI_WIN_EVT_KEY_DOWN) return keyboard_deliver_desktop_input((uint8_t)arg0, arg2) ? 0 : -1;
        if (event_type == GUI_WIN_EVT_MOUSE_WHEEL) {
            vga_scrollback_lines(arg2 * 3);
            return 0;
        }
    }
    return -1;
}

int kernel_gui_consume_desktop_open_path(char* path, size_t path_size) {
    if (!path || path_size == 0U) return -1;
    if (process_current_pid() != desktop_owner_pid || !desktop_open_path_pending) return -1;
    strncpy(path, desktop_open_path, path_size - 1U);
    path[path_size - 1U] = '\0';
    desktop_open_path_pending = 0;
    desktop_open_path[0] = '\0';
    return 0;
}

void handle_timer() {
    timer_ticks++;
    process_on_timer_tick();
    
    // Kernel level heartbeat: SAFE VGA WRITE at (79, 0)
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    vga[79] = (timer_ticks % 20 < 10) ? 0x1F2A : 0x1F20; // Star blinking in blue

    outb(0x20, 0x20);
}

void isr_handler_default() {
    outb(0x20, 0x20);
    outb(0xA0, 0x20);
}

void vga_print_int_hex(uint32_t n, char* buf) {
    const char* hex = "0123456789ABCDEF";
    buf[0] = '0'; buf[1] = 'x';
    for(int i=0; i<8; i++) {
        buf[9-i] = hex[(n >> (i*4)) & 0x0F];
    }
    buf[10] = '\0';
}

#if UINTPTR_MAX <= 0xFFFFFFFFU
static void panic_serial_hex(const char* label, uint32_t value) {
    serial_write("[panic] ");
    serial_write(label);
    serial_write("=");
    serial_write_hex32(value);
    serial_write_char('\n');
}
#endif

static void panic_serial_hex_u64(const char* label, uint64_t value) {
    serial_write("[panic] ");
    serial_write(label);
    serial_write("=");
    serial_write_hex64(value);
    serial_write_char('\n');
}

static void panic_serial_hex_uintptr(const char* label, uintptr_t value) {
#if UINTPTR_MAX > 0xFFFFFFFFU
    panic_serial_hex_u64(label, (uint64_t)value);
#else
    panic_serial_hex(label, (uint32_t)value);
#endif
}

static void panic_log_current_process() {
    process_t* current = process_current();

    if (!current) {
        serial_write_line("[panic] active pid=<none>");
        return;
    }

    serial_write("[panic] active pid=");
    serial_write_hex32((uint32_t)current->pid);
    serial_write(" ppid=");
    serial_write_hex32((uint32_t)current->parent_pid);
    serial_write(" kind=");
    serial_write(current->kind == PROCESS_KIND_USER ? "user" : "kernel");
    serial_write(" state=");
    if (current->state == PROC_RUNNABLE) serial_write("runnable");
    else if (current->state == PROC_RUNNING) serial_write("running");
    else if (current->state == PROC_ZOMBIE) serial_write("zombie");
    else serial_write("unused");
    serial_write(" name=");
    serial_write(current->name);
    if (current->image_path[0] != '\0') {
        serial_write(" image=");
        serial_write(current->image_path);
    }
    serial_write_char('\n');
}

static void panic_halt() {
    for (;;) {
        asm volatile("cli");
        asm volatile("hlt");
    }
}

static void boot_text_clear(uint8_t color) {
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    uint16_t cell = ((uint16_t)color << 8) | ' ';
    for (int i = 0; i < 80 * 25; i++) {
        vga[i] = cell;
    }
}

static void boot_text_write_line(int row, const char* text, uint8_t color) {
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    int col = 0;

    if (!text || row < 0 || row >= 25) return;
    while (text[col] != '\0' && col < 80) {
        vga[row * 80 + col] = ((uint16_t)color << 8) | (uint8_t)text[col];
        col++;
    }
}

static void boot_text_write_hex_line(int row, const char* label, uint32_t value, uint8_t color) {
    char hex_buf[16];
    char line[80];
    int off = 0;

    line[0] = '\0';
    if (label) {
        while (label[off] != '\0' && off < (int)sizeof(line) - 1) {
            line[off] = label[off];
            off++;
        }
        line[off] = '\0';
    }
    vga_print_int_hex(value, hex_buf);
    for (int i = 0; hex_buf[i] != '\0' && off < (int)sizeof(line) - 1; i++) {
        line[off++] = hex_buf[i];
    }
    line[off] = '\0';
    boot_text_write_line(row, line, color);
}

static void panic_text_exception(const char* title, const char* aux_label, uintptr_t aux_value, arch_trap_frame_t* frame) {
    boot_text_clear(0x1F);
    if (title) boot_text_write_line(0, title, 0x4F);
    if (aux_label) boot_text_write_hex_line(2, aux_label, (uint32_t)aux_value, 0x1F);
    if (frame) {
        boot_text_write_hex_line(4, "Error: ", (uint32_t)frame->error_code, 0x1F);
        boot_text_write_hex_line(5, "IP:    ", (uint32_t)arch_frame_user_ip(frame), 0x1F);
        boot_text_write_hex_line(6, "CS:    ", (uint32_t)frame->cs, 0x1F);
        boot_text_write_hex_line(7, "SP:    ", (uint32_t)arch_frame_user_sp(frame), 0x1F);
        boot_text_write_hex_line(8, "SS:    ", (uint32_t)frame->user_ss, 0x1F);
    }
    boot_text_write_line(10, "See serial log for more details.", 0x1E);
}

static const narcos_boot_info_t* boot_info_get(void) {
    const narcos_boot_info_t* info = (const narcos_boot_info_t*)(uintptr_t)NARCOS_BOOT_INFO_ADDR;

    if (info->magic != NARCOS_BOOT_INFO_MAGIC) return 0;
    if (info->version == 0U || info->version > NARCOS_BOOT_INFO_VERSION_MAX) return 0;
    if (info->size < NARCOS_BOOT_INFO_MIN_SIZE) return 0;
    return info;
}

static const char* boot_profile_name(uint8_t profile) {
    if (profile == 2U) return "safe-text";
    if (profile == 3U) return "serial-debug";
    return "normal";
}

static int boot_graphics_requested(void) {
    const narcos_boot_info_t* info = boot_info_get();

    if (!info) return 1;
    if ((info->flags & NARCOS_BOOT_FLAG_SAFE_TEXT) != 0U) return 0;
    return (info->flags & NARCOS_BOOT_FLAG_GRAPHICS) != 0U;
}

static void boot_log_info_handoff(void) {
    const narcos_boot_info_t* info = boot_info_get();

    if (!info) {
        serial_write_line("[boot] handoff: legacy/no boot info");
        return;
    }

    serial_write("[boot] handoff profile=");
    serial_write(boot_profile_name(info->profile));
    serial_write(" version=");
    serial_write_hex32((uint32_t)info->version);
    serial_write(" flags=");
    serial_write_hex32(info->flags);
    serial_write(" drive=");
    serial_write_hex32((uint32_t)info->boot_drive);
    serial_write(" kernel_lba=");
    serial_write_hex32(info->kernel_lba);
    serial_write(" sectors=");
    serial_write_hex32(info->kernel_sectors);
    serial_write_char('\n');

    serial_write("[boot] handoff video mode=");
    serial_write_hex32((uint32_t)info->vbe_mode);
    serial_write(" target_w=");
    serial_write_hex32((uint32_t)info->target_width);
    serial_write(" target_h=");
    serial_write_hex32((uint32_t)info->target_height);
    serial_write(" e820=");
    serial_write_hex32((uint32_t)info->e820_count);
    serial_write_char('\n');

    if (info->size >= sizeof(narcos_boot_info_t)) {
        serial_write("[boot] handoff framebuffer=");
        serial_write_hex32(info->framebuffer_addr);
        serial_write(" size=");
        serial_write_hex32(info->framebuffer_size);
        serial_write(" pitch=");
        serial_write_hex32((uint32_t)info->fb_pitch);
        serial_write(" bpp=");
        serial_write_hex32((uint32_t)info->fb_bpp);
        serial_write(" masks=");
        serial_write_hex32(((uint32_t)info->red_mask << 24) |
                           ((uint32_t)info->green_mask << 16) |
                           ((uint32_t)info->blue_mask << 8) |
                           (uint32_t)info->rsv_mask);
        serial_write_char('\n');

        serial_write("[boot] handoff kernel_load=");
        serial_write_hex32(info->kernel_load_addr);
        serial_write(" bytes=");
        serial_write_hex32(info->kernel_load_size);
        serial_write_char('\n');

        serial_write("[boot] handoff manifest=");
        serial_write_hex32((uint32_t)info->boot_manifest_version);
        serial_write(" kernel_entry=");
        serial_write_hex32(info->kernel_entry);
        serial_write(" kernel_crc=");
        serial_write_hex32(info->kernel_crc32);
        serial_write_char('\n');

        serial_write("[boot] handoff e820_map=");
        serial_write_hex32(info->e820_map_addr);
        serial_write(" entry_size=");
        serial_write_hex32((uint32_t)info->e820_entry_size);
        serial_write(" rsdp=");
        serial_write_hex32(info->rsdp_addr);
        serial_write_char('\n');

        serial_write("[boot] handoff initrd lba=");
        serial_write_hex32(info->initrd_lba);
        serial_write(" sectors=");
        serial_write_hex32(info->initrd_sectors);
        serial_write(" size=");
        serial_write_hex32(info->initrd_size);
        serial_write(" crc=");
        serial_write_hex32(info->initrd_crc32);
        serial_write_char('\n');
    }
}

static int boot_framebuffer_available() {
    if (!boot_graphics_requested()) return 0;
    return *(uint32_t*)(0x6100 + 40) != 0 &&
           vbe_get_width() != 0 &&
           vbe_get_height() != 0 &&
           vbe_get_bpp() != 0;
}

static void boot_fatal(const char* headline, const char* detail) {
    serial_write_line("");
    serial_write_line("[boot] fatal");
    if (headline) serial_write_line(headline);
    if (detail) serial_write_line(detail);

    if (boot_framebuffer_available()) {
        init_vbe();
        kernel_graphics_ready = 1;
        vbe_clear(0x180000);
        vbe_draw_string(20, 20, "NarcOs boot failed", 0xFFFFFF);
        if (headline) vbe_draw_string(20, 54, headline, 0xFFB3B3);
        if (detail) vbe_draw_string(20, 78, detail, 0xFFE4A3);
        vbe_draw_string(20, 112, "See serial log for more details.", 0xFFFFFF);
        vbe_update();
    } else {
        boot_text_clear(0x1F);
        boot_text_write_line(0, "NarcOs boot failed", 0x1F);
        if (headline) boot_text_write_line(2, headline, 0x4F);
        if (detail) boot_text_write_line(4, detail, 0x1F);
        boot_text_write_line(6, "See serial log for more details.", 0x1E);
    }

    panic_halt();
}

static void paging_probe_kernel_vm() {
    volatile uint32_t* direct;
    uint32_t* mapped;
    void* phys_page = alloc_physical_page();

    if (!phys_page) {
        boot_fatal("Kernel VM probe failed.",
                   "Could not allocate a physical page for paging API validation.");
    }

    mapped = (uint32_t*)paging_map_physical((uintptr_t)phys_page, 4096U, PAGING_FLAG_WRITE);
    if (!mapped) {
        free_physical_page(phys_page);
        boot_fatal("Kernel VM probe failed.",
                   "Dynamic virtual mapping window could not map a probe page.");
    }

    direct = (volatile uint32_t*)phys_page;
    mapped[0] = 0x4E415243U;
    mapped[1] = 0x4F532121U;
    if (direct[0] != 0x4E415243U || direct[1] != 0x4F532121U) {
        paging_unmap_virtual(mapped, 4096U);
        free_physical_page(phys_page);
        boot_fatal("Kernel VM probe failed.",
                   "Mapped writes were not visible through the identity mapping.");
    }

    paging_unmap_virtual(mapped, 4096U);
    free_physical_page(phys_page);
    serial_write("[boot] kernel_vm base=");
    serial_write_hex32(paging_kernel_vm_base());
    serial_write(" size=");
    serial_write_hex32(paging_kernel_vm_size());
    serial_write_char('\n');
}

static void panic_simple_exception(const char* tag, const char* title, uint32_t bg_color,
                                   const char* aux_label, uintptr_t aux_value, arch_trap_frame_t* frame) {
    char buf[64];

    serial_write_line("");
    serial_write("[panic] ");
    serial_write_line(tag);
    if (aux_label) panic_serial_hex_uintptr(aux_label, aux_value);
    panic_serial_hex_u64("error", (uint64_t)frame->error_code);
    panic_serial_hex_uintptr("ip", arch_frame_user_ip(frame));
    panic_serial_hex_u64("cs", (uint64_t)frame->cs);
    panic_serial_hex_uintptr("sp", arch_frame_user_sp(frame));
    panic_serial_hex_u64("ss", (uint64_t)frame->user_ss);
    panic_log_current_process();

    if (!kernel_graphics_ready) {
        panic_text_exception(title, aux_label, aux_value, frame);
    } else {
        vbe_clear(bg_color);
        vbe_draw_string(20, 20, title, 0xFFFFFF);
        if (aux_label) {
            vbe_draw_string(20, 50, aux_label, 0xFFFFFF);
            vga_print_int_hex((uint32_t)aux_value, buf);
            vbe_draw_string(130, 50, buf, 0xFFD27F);
        }
        vbe_draw_string(20, 68, "Error:", 0xFFFFFF);
        vga_print_int_hex((uint32_t)frame->error_code, buf);
        vbe_draw_string(130, 68, buf, 0xFFD27F);
        vbe_draw_string(20, 86, "IP:", 0xFFFFFF);
        vga_print_int_hex((uint32_t)arch_frame_user_ip(frame), buf);
        vbe_draw_string(130, 86, buf, 0xFFD27F);
        vbe_draw_string(20, 104, "CS:", 0xFFFFFF);
        vga_print_int_hex((uint32_t)frame->cs, buf);
        vbe_draw_string(130, 104, buf, 0xFFD27F);
        vbe_draw_string(20, 122, "SP:", 0xFFFFFF);
        vga_print_int_hex((uint32_t)arch_frame_user_sp(frame), buf);
        vbe_draw_string(130, 122, buf, 0xFFD27F);
        vbe_update();
    }

    panic_halt();
}

static void wait_8042_input_clear() {
    for (uint32_t i = 0; i < 0x10000U; i++) {
        if ((inb(0x64) & 0x02U) == 0U) return;
    }
}

static void reboot_system() {
    serial_write_line("[sys] reboot requested");
    vga_println("Rebooting...");
    asm volatile("cli");

    wait_8042_input_clear();
    outb(0x64, 0xFE);

    outb(0xCF9, 0x02);
    outb(0xCF9, 0x06);

    {
        struct {
            uint16_t limit;
            uint32_t base;
        } __attribute__((packed)) null_idtr = {0, 0};
        asm volatile("lidt %0" : : "m"(null_idtr));
        asm volatile("int3");
    }

    panic_halt();
}

static void poweroff_system() {
    serial_write_line("[sys] poweroff requested");
    vga_println("Powering off...");
    asm volatile("cli");

    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);

    panic_halt();
}

static void print_kernel_log() {
    static char log_buf[4096];
    int len = serial_copy_ring_buffer(log_buf, sizeof(log_buf));

    if (len <= 0) {
        vga_println("Kernel log is empty.");
        return;
    }

    vga_print(log_buf);
    if (len > 0 && log_buf[len - 1] != '\n') vga_println("");
}

static void print_pci_id_line(const char* label, const pci_device_info_t* dev) {
    char buf[11];

    vga_print(label);
    if (!dev) {
        vga_println("none");
        return;
    }

    vga_print_int_hex((uint32_t)dev->vendor_id, buf);
    vga_print(buf + 6);
    vga_print(":");
    vga_print_int_hex((uint32_t)dev->device_id, buf);
    vga_print(buf + 6);
    vga_print(" @ ");
    vga_print_int_hex((uint32_t)dev->bus, buf);
    vga_print(buf + 8);
    vga_print(":");
    vga_print_int_hex((uint32_t)dev->slot, buf);
    vga_print(buf + 8);
    vga_print(".");
    vga_print_int_hex((uint32_t)dev->func, buf);
    vga_print(buf + 8);
    vga_println("");
}

static void print_pci_irq_line(const char* label, const pci_device_info_t* dev) {
    pci_irq_route_t route;

    vga_print(label);
    if (!dev || pci_decode_irq(dev, &route) != 0) {
        vga_println("none");
        return;
    }

    vga_print(pci_irq_pin_name(route.irq_pin));
    if (route.routed) {
        vga_print(" -> IRQ ");
        vga_print_int(route.irq_line);
        vga_println(route.masked ? " (masked)" : " (enabled)");
    } else {
        vga_println(" -> unrouted");
    }
}

static void print_hardware_info() {
    const cpu_info_t* cpu = cpu_get_info();
    pci_device_info_t devices[64];
    const pci_device_info_t* storage_dev = 0;
    const pci_device_info_t* network_dev = 0;
    const pci_device_info_t* display_dev = 0;
    const pci_device_info_t* usb_dev = 0;
    int pci_total;
    int storage_count = 0;
    int network_count = 0;
    int display_count = 0;
    int usb_count = 0;
    int bridge_count = 0;
    int other_count = 0;
    net_ipv4_config_t netcfg;
    char buf[64];

    pci_total = pci_enumerate(devices, 64);
    if (pci_total > 64) pci_total = 64;
    for (int i = 0; i < pci_total; i++) {
        const pci_device_info_t* dev = &devices[i];
        if (dev->class_code == 0x01) {
            storage_count++;
            if (!storage_dev) storage_dev = dev;
        } else if (dev->class_code == 0x02) {
            network_count++;
            if (!network_dev) network_dev = dev;
        } else if (dev->class_code == 0x03) {
            display_count++;
            if (!display_dev) display_dev = dev;
        } else if (dev->class_code == 0x0C && dev->subclass == 0x03) {
            usb_count++;
            if (!usb_dev) usb_dev = dev;
        } else if (dev->class_code == 0x06) {
            bridge_count++;
        } else {
            other_count++;
        }
    }

    vga_println("Hardware Info");
    vga_print("  CPU Vendor : ");
    vga_println(cpu->vendor);
    vga_print("  CPUID      : ");
    vga_println(cpu->cpuid_supported ? "yes" : "no");
    vga_print("  SSE        : ");
    vga_println(cpu->sse_enabled ? "enabled" : (cpu->sse_supported ? "supported, disabled" : "not supported"));
    vga_print("  PSE        : ");
    vga_println(cpu->pse_supported ? "yes" : "no");
    vga_print("  APIC       : ");
    vga_println(cpu->apic_supported ? "yes" : "no");
    vga_print("  TSC        : ");
    vga_println(cpu->tsc_supported ? "yes" : "no");
    vga_print("  Video      : ");
    if (screen_is_graphics_enabled()) {
        vga_print_int(vbe_get_width());
        vga_print("x");
        vga_print_int(vbe_get_height());
        vga_print(" @ ");
        vga_print_int(vbe_get_bpp());
        vga_println("bpp");
    } else {
        vga_println("text-mode fallback");
    }
    vga_print("  Managed RAM: ");
    vga_print_int((int)(paging_total_frames() / 256U));
    vga_println(" MB");
    vga_print("  PCI Count  : ");
    vga_print_int(pci_device_count());
    vga_println("");
    vga_println("  PCI Classes:");
    vga_print("    Storage : ");
    vga_print_int(storage_count);
    vga_println("");
    vga_print("    Network : ");
    vga_print_int(network_count);
    vga_println("");
    vga_print("    Display : ");
    vga_print_int(display_count);
    vga_println("");
    vga_print("    USB     : ");
    vga_print_int(usb_count);
    vga_println("");
    vga_print("    Bridge  : ");
    vga_print_int(bridge_count);
    vga_println("");
    vga_print("    Other   : ");
    vga_print_int(other_count);
    vga_println("");
    print_pci_id_line("  First Storage : ", storage_dev);
    if (storage_dev) {
        vga_print("  Storage Ifc   : ");
        vga_println(pci_storage_controller_name(storage_dev));
        print_pci_irq_line("  Storage IRQ   : ", storage_dev);
    }
    vga_print("  Storage Path  : ");
    vga_println(storage_backend_name());
    vga_print("  Storage Base  : ");
    vga_print_int((int)storage_volume_base_lba());
    vga_print(" (");
    if (storage_volume_scheme() == STORAGE_PARTITION_SCHEME_GPT) vga_print("GPT");
    else if (storage_volume_scheme() == STORAGE_PARTITION_SCHEME_MBR) vga_print("MBR");
    else vga_print("RAW");
    vga_print(")");
    vga_println("");
    print_pci_id_line("  First Network : ", network_dev);
    if (network_dev) {
        print_pci_irq_line("  Network IRQ   : ", network_dev);
    }
    print_pci_id_line("  First Display : ", display_dev);
    print_pci_id_line("  First USB     : ", usb_dev);
    if (net_get_ipv4_config(&netcfg) == 0 && netcfg.available) {
        vga_print("  Network    : ");
        vga_println(netcfg.configured ? "available + configured" : "available + unconfigured");
        vga_print("  IP         : ");
        vga_print_int((int)((netcfg.ip_addr >> 24) & 0xFF));
        vga_print(".");
        vga_print_int((int)((netcfg.ip_addr >> 16) & 0xFF));
        vga_print(".");
        vga_print_int((int)((netcfg.ip_addr >> 8) & 0xFF));
        vga_print(".");
        vga_print_int((int)(netcfg.ip_addr & 0xFF));
        vga_println("");
    } else {
        vga_println("  Network    : unavailable");
    }
    vga_print("  Uptime     : ");
    vga_print_int((int)(timer_ticks / 100));
    vga_println("s");
    vga_print("  CPUID max  : ");
    vga_print_int_hex(cpu->max_basic_leaf, buf);
    vga_println(buf);
}

void gpf_handler(arch_trap_frame_t* frame) {
    serial_write_line("");
    serial_write_line("[panic] general protection fault");
    panic_serial_hex_u64("error", (uint64_t)frame->error_code);
    panic_serial_hex_uintptr("ip", arch_frame_user_ip(frame));
    panic_serial_hex_u64("cs", (uint64_t)frame->cs);
    panic_serial_hex_uintptr("sp", arch_frame_user_sp(frame));
    panic_serial_hex_u64("ss", (uint64_t)frame->user_ss);
    panic_log_current_process();
    process_debug_dump("gpf");

    if (!kernel_graphics_ready) {
        panic_text_exception("!!! NARC-OS GPF (RING 3 CRASH) !!!", 0, 0, frame);
        panic_halt();
    }

    vbe_clear(0x880000); // Red
    vbe_draw_string(20, 20, "!!! NARC-OS GPF (RING 3 CRASH) !!!", 0xFFFFFF);
    
    char buf[64];
#if UINTPTR_MAX > 0xFFFFFFFFU
    vbe_draw_string(20, 50, "IP:", 0xFFFFFF);
    vga_print_int_hex((uint32_t)arch_frame_user_ip(frame), buf);
    vbe_draw_string(100, 50, buf, 0xFFFF00);
    vbe_draw_string(20, 68, "SP:", 0xFFFFFF);
    vga_print_int_hex((uint32_t)arch_frame_user_sp(frame), buf);
    vbe_draw_string(100, 68, buf, 0xFFFF00);
    vbe_draw_string(20, 86, "See serial log for full 64-bit trap state.", 0xFFFFFF);
#else
    // GS, FS, ES, DS, EDI, ESI, EBP, ESP?, EBX, EDX, ECX, EAX
    const char* reg_names[] = {"GS", "FS", "ES", "DS", "EDI", "ESI", "EBP", "ESP_U", "EBX", "EDX", "ECX", "EAX"};
    uint32_t* raw = (uint32_t*)frame;
    
    for(int i=0; i<12; i++) {
        vbe_draw_string(20, 50 + (i*15), reg_names[i], 0xFFFFFF);
        vga_print_int_hex(raw[i], buf);
        vbe_draw_string(80, 50 + (i*15), buf, 0xCCCCCC);
    }
    
    vbe_draw_string(250, 50, "HW-ERR:", 0xFFFFFF);
    vga_print_int_hex(frame->error_code, buf);
    vbe_draw_string(350, 50, buf, 0xFFFF00);

    vbe_draw_string(250, 65, "HW-EIP:", 0xFFFFFF);
    vga_print_int_hex(frame->eip, buf);
    vbe_draw_string(350, 65, buf, 0xFFFF00);

    vbe_draw_string(250, 80, "HW-CS:", 0xFFFFFF);
    vga_print_int_hex(frame->cs, buf);
    vbe_draw_string(350, 80, buf, 0xFFFF00);

    vbe_draw_string(250, 95, "HW-ESP:", 0xFFFFFF);
    vga_print_int_hex(frame->user_esp, buf);
    vbe_draw_string(350, 95, buf, 0xFFFF00);

    vbe_draw_string(250, 110, "HW-SS:", 0xFFFFFF);
    vga_print_int_hex(frame->user_ss, buf);
    vbe_draw_string(350, 110, buf, 0xFFFF00);
#endif

    vbe_update();
    panic_halt();
}

void page_fault_handler(arch_trap_frame_t* frame) {
    panic_log_current_process();
    process_debug_dump("page-fault");
    panic_simple_exception("page fault", "!!! NARC-OS PAGE FAULT !!!", 0x1A0000,
                           "Fault Addr:", arch_read_fault_address(), frame);
}

void invalid_opcode_handler(arch_trap_frame_t* frame) {
    panic_simple_exception("invalid opcode", "!!! NARC-OS INVALID OPCODE !!!", 0x341400,
                           0, 0, frame);
}

void stack_fault_handler(arch_trap_frame_t* frame) {
    panic_simple_exception("stack fault", "!!! NARC-OS STACK FAULT !!!", 0x301400,
                           0, 0, frame);
}

void user_code_test_logic() {
    // Super simple loop. No strings, no complex stack.
    while(1) {
        asm volatile (
            "mov $4, %%eax\n" // SYS_GUI_UPDATE
            "int $0x80"
            : : : "eax"
        );
        for(int i=0; i<50000; i++) asm volatile("nop");
    }
}

void vbe_compose_scene_basic() {
    // This is a simplified version of the composer for testing
    // It's called from SYSCALL_GUI_UPDATE to bypass the blocked kmain loop
    extern window_t windows[MAX_WINDOWS];
    extern int window_count, active_window_idx;
    extern int current_dir_index;
    int mx = get_mouse_x();
    int my = get_mouse_y();
    
    // Use the global state to redraw the screen
    vbe_compose_scene(windows, window_count, active_window_idx, 0,
                      legacy_desktop_dir_index >= 0 ? legacy_desktop_dir_index : current_dir_index,
                      -1, mx, my, 0, 0, 0, 0, 0, -1);
    vbe_present_composition_with_cursor(mx, my);
}

static int run_usermode_test_command(void) {
#if UINTPTR_MAX > 0xFFFFFFFFU
    vga_println("usermode_test is only available on the legacy i386 path.");
    return -1;
#else
    extern void jump_to_usermode_v9(uint32_t eip, uint32_t esp, uint32_t lfb);
    extern void usermode_entry_gate();
    uint32_t user_esp = 0x90000;
    uint32_t lfb_addr = *(uint32_t*)(0x6100 + 40);
    uint32_t target_eip = (uint32_t)usermode_entry_gate;
    uint32_t* magic_ptr = (uint32_t*)(target_eip - 4);
    char buf[64];

    vga_println("Launching Secure User Mode Test V12 (Final Victory)...");
    vga_print("Target EIP Sym: ");
    vga_print_int_hex(target_eip, buf);
    vga_println(buf);

    if (*magic_ptr != 0xDEADC0DE) {
        vga_println("CRITICAL: MAGIC NUMBER MISMATCH!");
        return -1;
    }

    vga_println("Magic Recognized. Transitioning to Ring 3...");
    vga_println("Verification: If the heartbeat pixel is rotating and the");
    vga_println("mouse is responsive, the transition was successful!");

    arch_set_kernel_stack(KERNEL_BOOT_STACK_TOP);
    jump_to_usermode_v9(target_eip, user_esp, lfb_addr);
    return 0;
#endif
}

static int kernel_snapshot_contains_pid(int pid) {
    process_snapshot_entry_t entries[16];
    int count = process_snapshot(entries, (int)(sizeof(entries) / sizeof(entries[0])));

    if (count < 0) return 0;
    for (int i = 0; i < count; i++) {
        if (entries[i].pid == pid) return 1;
    }
    return 0;
}

static void kernel_waitpid_test_child(void* arg) {
    (void)arg;
    while (!kernel_waitpid_test_release) process_yield();
}

static void kernel_pipe_test_writer(void* arg) {
    process_t* current = process_current();
    int write_fd = (int)(uintptr_t)arg;
    uint8_t buffer[128];
    uint32_t remaining = kernel_pipe_test_state.target_bytes;

    kernel_pipe_test_state.writer_status = -1;
    if (!current) return;
    memset(buffer, 'P', sizeof(buffer));
    (void)fd_close(current, kernel_pipe_test_state.read_fd);

    while (remaining != 0U) {
        uint32_t chunk_len = remaining;
        int rc;

        if (chunk_len > sizeof(buffer)) chunk_len = sizeof(buffer);
        rc = fd_write(current, write_fd, buffer, chunk_len);
        if (rc <= 0) {
            kernel_pipe_test_state.writer_status = -2;
            return;
        }
        kernel_pipe_test_state.bytes_written += (uint32_t)rc;
        remaining -= (uint32_t)rc;
        process_yield();
    }

    kernel_pipe_test_state.writer_status = fd_close(current, write_fd) == 0 ? 0 : -3;
}

static void kernel_pipe_test_reader(void* arg) {
    process_t* current = process_current();
    int read_fd = (int)(uintptr_t)arg;
    uint8_t buffer[96];

    kernel_pipe_test_state.reader_status = -1;
    if (!current) return;
    (void)fd_close(current, kernel_pipe_test_state.write_fd);

    for (;;) {
        int rc = fd_read(current, read_fd, buffer, sizeof(buffer));

        if (rc < 0) {
            kernel_pipe_test_state.reader_status = -2;
            return;
        }
        if (rc == 0) break;
        kernel_pipe_test_state.bytes_read += (uint32_t)rc;
        process_yield();
    }

    kernel_pipe_test_state.reader_status = fd_close(current, read_fd) == 0 ? 0 : -3;
}

static int run_process_model_selftest(void) {
    int pid;
    int status = -1;
    int wait_rc;

    kernel_waitpid_test_release = 0;
    pid = process_create_kernel("waitpid-test", kernel_waitpid_test_child, 0);
    if (pid < 0) {
        vga_println("proc_test: spawn failed.");
        return -1;
    }

    wait_rc = process_waitpid_sync_current(pid, WAITPID_FLAG_NOHANG, &status);
    if (wait_rc != 0) {
        kernel_waitpid_test_release = 1;
        (void)process_waitpid_sync_current(pid, 0U, 0);
        vga_println("proc_test: WAITPID_FLAG_NOHANG failed.");
        return -1;
    }

    kernel_waitpid_test_release = 1;
    wait_rc = process_waitpid_sync_current(pid, 0U, &status);
    if (wait_rc != pid || status != 0) {
        vga_println("proc_test: waitpid returned wrong result.");
        return -1;
    }

    wait_rc = process_waitpid_sync_current(pid, WAITPID_FLAG_NOHANG, &status);
    if (wait_rc != -1) {
        vga_println("proc_test: reaped child was still waitable.");
        return -1;
    }

    if (kernel_snapshot_contains_pid(pid)) {
        vga_println("proc_test: zombie cleanup failed.");
        return -1;
    }

    vga_println("proc_test: ok");
    return 0;
}

static int run_pipe_selftest(void) {
    process_t* current = process_current();
    int pipefd[2] = { -1, -1 };
    int reader_pid = -1;
    int writer_pid = -1;
    int wait_status = 0;

    if (!current) {
        vga_println("pipe_test: no current process.");
        return -1;
    }

    memset(&kernel_pipe_test_state, 0, sizeof(kernel_pipe_test_state));
    kernel_pipe_test_state.target_bytes = PIPE_BUFFER_SIZE * 3U + 73U;

    if (fd_pipe(current, pipefd) != 0) {
        vga_println("pipe_test: pipe creation failed.");
        return -1;
    }

    kernel_pipe_test_state.read_fd = pipefd[0];
    kernel_pipe_test_state.write_fd = pipefd[1];
    reader_pid = process_create_kernel("pipe-reader", kernel_pipe_test_reader, (void*)(uintptr_t)pipefd[0]);
    if (reader_pid < 0) goto fail;
    writer_pid = process_create_kernel("pipe-writer", kernel_pipe_test_writer, (void*)(uintptr_t)pipefd[1]);
    if (writer_pid < 0) goto fail;

    (void)fd_close(current, pipefd[0]);
    pipefd[0] = -1;
    (void)fd_close(current, pipefd[1]);
    pipefd[1] = -1;

    if (process_waitpid_sync_current(writer_pid, 0U, &wait_status) != writer_pid) goto fail;
    if (process_waitpid_sync_current(reader_pid, 0U, &wait_status) != reader_pid) goto fail;

    if (kernel_pipe_test_state.writer_status != 0 || kernel_pipe_test_state.reader_status != 0) goto fail;
    if (kernel_pipe_test_state.bytes_written != kernel_pipe_test_state.target_bytes) goto fail;
    if (kernel_pipe_test_state.bytes_read != kernel_pipe_test_state.target_bytes) goto fail;
    if (kernel_pipe_test_state.bytes_read != kernel_pipe_test_state.bytes_written) goto fail;

    vga_println("pipe_test: ok");
    return 0;

fail:
    if (pipefd[0] >= 0) (void)fd_close(current, pipefd[0]);
    if (pipefd[1] >= 0) (void)fd_close(current, pipefd[1]);
    if (writer_pid > 0) (void)process_waitpid_sync_current(writer_pid, 0U, 0);
    if (reader_pid > 0) (void)process_waitpid_sync_current(reader_pid, 0U, 0);
    vga_println("pipe_test: failed.");
    return -1;
}

int kernel_run_privileged_command(int cmd, const char* arg) {
    const char* value = arg ? arg : "";
    int graphics_mode = screen_is_graphics_enabled();

    switch (cmd) {
        case PRIV_CMD_EDIT:
            if (value[0] == '\0') return -1;
            editor_start(value);
            clear_screen();
            return 0;
        case PRIV_CMD_MEM:
            print_memory_info();
            return 0;
        case PRIV_CMD_MALLOC_TEST:
            malloc_test();
            return 0;
        case PRIV_CMD_USERMODE_TEST:
            if (!graphics_mode) return -1;
            return run_usermode_test_command();
        case PRIV_CMD_HWINFO:
            print_hardware_info();
            return 0;
        case PRIV_CMD_PCI:
            pci_print_devices();
            return 0;
        case PRIV_CMD_STORAGE:
            pci_print_storage_devices();
            storage_print_status();
            return 0;
        case PRIV_CMD_LOG:
            print_kernel_log();
            return 0;
        case PRIV_CMD_REBOOT:
            reboot_system();
            return 0;
        case PRIV_CMD_POWEROFF:
            poweroff_system();
            return 0;
        case PRIV_CMD_PROC_DUMP:
            process_debug_dump(value[0] != '\0' ? value : "manual");
            vga_println("process dump written to serial.");
            return 0;
        case PRIV_CMD_PROC_TEST:
            return run_process_model_selftest();
        case PRIV_CMD_PIPE_TEST:
            return run_pipe_selftest();
        default:
            return -1;
    }
}

void execute_command(char* cmd) {
    int status;
    if (!cmd) return;
    if (strcmp(cmd, "http") == 0 || strncmp(cmd, "http ", 5) == 0) {
        const char* arg = cmd + 4;
        while (*arg == ' ') arg++;
        (void)net_http_command(arg);
        return;
    }
    status = run_user_shell_command(cmd);
    if (status < 0) {
        vga_print_color("shell command failed: ", 0x0C);
        vga_print_int(status);
        vga_println("");
    }
}

extern char cmd_to_execute[128];
extern volatile int cmd_ready;
extern int current_dir_index;
extern void get_current_dir_name(char* buf);

void print_prompt() {
    vga_print_color("root@narc:", 0x0A);
    char path[256];
    fs_get_current_path(path, sizeof(path));
    vga_print_color(path, 0x0B);
    vga_print_color("$ ", 0x0A);
}

static void print_text_fallback_banner() {
    vga_print_color("\n  NarcOs Text Fallback Mode\n", 0x0E);
    vga_print_color("  ========================\n", 0x0E);
    vga_println("  Graphics init unavailable; continuing on VGA text console.");
    vga_println("  GUI apps are disabled in this mode.");
    print_prompt();
}

static void print_gui_terminal_banner(void) {
    vga_println("");
    vga_print_color("  NarcOs Terminal\n", 0x0B);
    vga_print_color("  ===============\n", 0x08);
    vga_println("  Local shell session is ready.");
    vga_println("  Try: help, ls, pwd, ps, date, time");
    vga_println("");
}

static void console_process_main(void) {
    for (;;) {
        if (cmd_ready) {
            execute_command(cmd_to_execute);
            cmd_ready = 0;
            print_prompt();
        }
        process_poll();
        asm volatile("hlt");
        process_poll();
    }
}

static void console_process_entry(void* arg) {
    (void)arg;
    console_process_main();
}

static void graphics_process_main(void) {
    uint32_t last_clock_tick = timer_ticks;
    int last_mx = get_mouse_x();
    int last_my = get_mouse_y();
    int last_lp = mouse_left_pressed();
    int last_rp = mouse_right_pressed();
    vbe_set_cursor_mode(CURSOR_MODE_ARROW);
    vbe_compose_scene(windows, window_count, active_window_idx, 0,
                      legacy_desktop_dir_index >= 0 ? legacy_desktop_dir_index : current_dir_index,
                      -1, last_mx, last_my, 0, 0, 0, 0, 0, -1);
    vbe_present_composition_with_cursor(last_mx, last_my);
    gui_dirty_valid = 0;
    gui_needs_redraw = 0;

    for (;;) {
        int mx = get_mouse_x();
        int my = get_mouse_y();
        int lp = mouse_left_pressed();
        int rp = mouse_right_pressed();
        int mouse_moved = mouse_consume_moved();
        int mouse_wheel = mouse_consume_wheel();
        int redraw = 0;

        if (cmd_ready) {
            execute_command(cmd_to_execute);
            cmd_ready = 0;
            print_prompt();
            gui_mark_dirty_full();
            gui_needs_redraw = 1;
        }
        desktop_watchdog_tick();
        if (timer_ticks - last_clock_tick >= 100U) {
            read_rtc();
            last_clock_tick = timer_ticks;
            if (!nwm_desktop_surface_active_state()) {
                gui_mark_dirty_full();
                gui_needs_redraw = 1;
            }
        }

        if (mouse_wheel != 0) {
            nwm_queue_desktop_event(GUI_WIN_EVT_MOUSE_WHEEL, (int16_t)mx, (int16_t)my, mouse_wheel);
        }
        if (mouse_moved || mx != last_mx || my != last_my) {
            nwm_queue_desktop_event(GUI_WIN_EVT_MOUSE_MOVE, (int16_t)mx, (int16_t)my, 0);
        }
        if (lp != last_lp) {
            nwm_queue_desktop_event(lp ? GUI_WIN_EVT_MOUSE_DOWN : GUI_WIN_EVT_MOUSE_UP,
                                    (int16_t)mx, (int16_t)my, lp ? 1 : 0);
        }
        if (rp != last_rp) {
            nwm_queue_desktop_event(rp ? GUI_WIN_EVT_MOUSE_DOWN : GUI_WIN_EVT_MOUSE_UP,
                                    (int16_t)mx, (int16_t)my, 2);
        }

        run_user_tasks();
        if (gui_needs_redraw || redraw) {
            int present_full;
            present_full = !gui_dirty_valid;
            gui_mark_dirty_rect(last_mx - 2, last_my - 2, 18, 18);
            gui_mark_dirty_rect(mx - 2, my - 2, 18, 18);
            if (present_full) {
                vbe_compose_scene(windows, window_count, active_window_idx, 0,
                                  legacy_desktop_dir_index >= 0 ? legacy_desktop_dir_index : current_dir_index,
                                  -1, mx, my, 0, 0, 0, 0, 0, -1);
            } else {
                vbe_compose_scene_region(windows, window_count, active_window_idx, 0,
                                         legacy_desktop_dir_index >= 0 ? legacy_desktop_dir_index : current_dir_index,
                                         -1, mx, my, 0, 0, 0, 0, 0, -1,
                                         gui_dirty_x, gui_dirty_y, gui_dirty_w, gui_dirty_h);
            }
            wait_vsync();
            if (present_full) {
                vbe_present_composition_with_cursor(mx, my);
            } else {
                vbe_present_composition_region(gui_dirty_x, gui_dirty_y, gui_dirty_w, gui_dirty_h);
                vbe_render_mouse_direct(mx, my);
            }
            gui_dirty_valid = 0;
            gui_needs_redraw = 0;
            last_mx = mx;
            last_my = my;
            last_lp = lp;
            last_rp = rp;
        } else if (mouse_moved || mx != last_mx || my != last_my) {
            vbe_present_cursor_fast(last_mx, last_my, mx, my);
            last_mx = mx;
            last_my = my;
            last_lp = lp;
            last_rp = rp;
        }
        process_poll();
        asm volatile("hlt");
        process_poll();
    }
}

static void graphics_process_entry(void* arg) {
    (void)arg;
    graphics_process_main();
}

static void desktop_service_tick(void) {
    static uint32_t next_retry_tick = 0;
    process_snapshot_entry_t entries[16];
    int count;
    int desktop_alive = 0;

    if (!screen_is_graphics_enabled()) return;
    if (timer_ticks < next_retry_tick) return;

    count = process_snapshot(entries, 16);
    if (count > 0) {
        for (int i = 0; i < count; i++) {
            if (strcmp(entries[i].image_path, "/bin/desktop") == 0 ||
                strcmp(entries[i].name, "desktop") == 0) {
                if (entries[i].state == PROC_RUNNABLE || entries[i].state == PROC_RUNNING) {
                    desktop_alive = 1;
                    break;
                }
            }
        }
    }

    if (desktop_alive) return;

    {
        static const char* desktop_argv[] = {"/bin/desktop"};
        int pid = process_create_user("/bin/desktop", desktop_argv, 1, 0U);

        if (pid >= 0) {
            next_retry_tick = timer_ticks + 200U;
        } else {
            next_retry_tick = timer_ticks + 100U;
        }
    }
}

static void service_process_main(void* arg) {
    (void)arg;
    for (;;) {
        desktop_service_tick();
        net_poll();
        process_poll();
        asm volatile("hlt");
        process_poll();
    }
}

void kmain() {
    const cpu_info_t* cpu;
    int console_pid;
    int graphics_pid;
    int service_pid;

    serial_init();
    serial_write_line("[boot] kmain");
    boot_log_info_handoff();
    arch_init_cpu();
    cpu = cpu_get_info();
    if (!cpu->cpuid_supported || !cpu->pse_supported) {
        boot_fatal("Unsupported CPU detected.",
                   "Current kernel requires CPUID and 4 MB page support (PSE).");
    }
    serial_write_line("[boot] init_pic");
    arch_init_legacy_pic();
    serial_write_line("[boot] init_pit");
    arch_init_timer(100U);
    arch_init_interrupts();
    serial_write_line("[boot] init_paging");
    arch_init_memory();
    serial_write("[boot] paging total_frames=");
    serial_write_hex32(paging_total_frames());
    serial_write(" used_frames=");
    serial_write_hex32(paging_used_frames());
    serial_write_char('\n');
    serial_write("[boot] kernel_stack base=");
    serial_write_hex32(paging_kernel_stack_base());
    serial_write(" size=");
    serial_write_hex32(paging_kernel_stack_size());
    serial_write_char('\n');
    paging_probe_kernel_vm();
    if (init_usermode() != 0) {
        boot_fatal("User memory initialization failed.",
                   "Ring 3 code/data alias mappings could not be established.");
    }
    usermode_debug_dump("post-usermode");
    init_keyboard();
    usermode_debug_dump("post-kbd");
    storage_init();
    usermode_debug_dump("post-storage");
    init_fs();
    legacy_desktop_dir_index = fs_find_node("/home/user/Desktop");
    usermode_debug_dump("post-fs");
    rtc_init_timezone();
    read_rtc();
    usermode_debug_dump("post-rtc");
    init_heap();
    usermode_debug_dump("post-heap");
    screen_set_graphics_enabled(0);
    if (boot_framebuffer_available()) {
        serial_write_line("[boot] init_vbe");
        init_vbe();
        usermode_debug_dump("post-init_vbe");
        kernel_graphics_ready = 1;
        screen_set_graphics_enabled(1);
        usermode_debug_dump("post-graphics-flag");
        init_mouse();
        usermode_debug_dump("post-init_mouse");
    } else {
        serial_write_line("[boot] framebuffer unavailable, using text fallback");
        kernel_graphics_ready = 0;
    }
    if (init_usermode() != 0) {
        boot_fatal("User memory reinitialization failed.",
                   "Ring 3 runtime state could not be restored after display setup.");
    }
    usermode_debug_dump("post-usermode-reinit");
    usermode_debug_dump("post-vbe");
    net_init();
    usermode_debug_dump("post-net");
    serial_write_line("[boot] process_init");
    process_init();
    usermode_debug_dump("post-procinit");
    console_pid = -1;
    graphics_pid = -1;
    if (!screen_is_graphics_enabled()) {
        console_pid = process_create_kernel("console", console_process_entry, 0);
    } else {
        graphics_pid = process_create_kernel("gui-host", graphics_process_entry, 0);
    }
    service_pid = process_create_kernel("service", service_process_main, 0);
    if ((!screen_is_graphics_enabled() && console_pid < 0) ||
        (screen_is_graphics_enabled() && graphics_pid < 0) ||
        service_pid < 0) {
        boot_fatal("Scheduler bootstrap failed.",
                   "Required kernel service tasks could not be created with the current memory map.");
    }
    if (screen_is_graphics_enabled()) {
        serial_write_line("[boot] legacy desktop bootstrap");
    } else {
        serial_write_line("[boot] text fallback ready");
    }
    clear_screen();
    if (screen_is_graphics_enabled()) {
        nwm_init_windows();
        print_gui_terminal_banner();
        print_prompt();
    } else {
        print_text_fallback_banner();
    }
    usermode_debug_dump("pre-sched");
    serial_write_line("[boot] scheduler start");
    scheduler_start();
}
