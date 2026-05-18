#include "user_lib.h"
#include "doomgeneric.h"
#include "doomkeys.h"

#define DOOM_KEYQUEUE_SIZE 128
#define DOOM_MAX_ARGS 16
#define DOOM_DEFAULT_WAD "/assets/doom1.wad"
#define DOOM_CONFIG_DIR "/home/user/doom"
#define DOOM_CONFIG_FILE "/home/user/doom/default.cfg"

typedef struct {
    unsigned short events[DOOM_KEYQUEUE_SIZE];
    unsigned int read_idx;
    unsigned int write_idx;
    int window_id;
    uint32_t* present_pixels;
    uint32_t present_capacity;
    uint16_t* x_map;
    uint32_t x_map_capacity;
    int x_map_width;
    int width;
    int height;
    int running;
} narcos_doom_state_t;

static narcos_doom_state_t doom_state;

static int doom_has_arg(int argc, char** argv, const char* key) {
    for (int i = 1; i < argc; i++) {
        if (argv[i] && userlib_strcmp(argv[i], key) == 0) return 1;
    }
    return 0;
}

static unsigned char doom_scancode_to_key(int scancode) {
    switch (scancode & 0x7F) {
        case 0x01: return KEY_ESCAPE;
        case 0x02: return '1';
        case 0x03: return '2';
        case 0x04: return '3';
        case 0x05: return '4';
        case 0x06: return '5';
        case 0x07: return '6';
        case 0x08: return '7';
        case 0x09: return '8';
        case 0x0A: return '9';
        case 0x0B: return '0';
        case 0x0C: return KEY_MINUS;
        case 0x0D: return KEY_EQUALS;
        case 0x0E: return KEY_BACKSPACE;
        case 0x0F: return KEY_TAB;
        case 0x10: return 'q';
        case 0x11: return 'w';
        case 0x12: return 'e';
        case 0x13: return 'r';
        case 0x14: return 't';
        case 0x15: return 'y';
        case 0x16: return 'u';
        case 0x17: return 'i';
        case 0x18: return 'o';
        case 0x19: return 'p';
        case 0x1C: return KEY_ENTER;
        case 0x1D: return KEY_FIRE;
        case 0x1E: return 'a';
        case 0x1F: return 's';
        case 0x20: return 'd';
        case 0x21: return 'f';
        case 0x22: return 'g';
        case 0x23: return 'h';
        case 0x24: return 'j';
        case 0x25: return 'k';
        case 0x26: return 'l';
        case 0x2A: return KEY_RSHIFT;
        case 0x2C: return 'z';
        case 0x2D: return 'x';
        case 0x2E: return 'c';
        case 0x2F: return 'v';
        case 0x30: return 'b';
        case 0x31: return 'n';
        case 0x32: return 'm';
        case 0x36: return KEY_RSHIFT;
        case 0x38: return KEY_LALT;
        case 0x39: return KEY_USE;
        case 0x3B: return KEY_F1;
        case 0x3C: return KEY_F2;
        case 0x3D: return KEY_F3;
        case 0x3E: return KEY_F4;
        case 0x3F: return KEY_F5;
        case 0x40: return KEY_F6;
        case 0x41: return KEY_F7;
        case 0x42: return KEY_F8;
        case 0x43: return KEY_F9;
        case 0x44: return KEY_F10;
        case 0x48: return KEY_UPARROW;
        case 0x4B: return KEY_LEFTARROW;
        case 0x4D: return KEY_RIGHTARROW;
        case 0x50: return KEY_DOWNARROW;
        default: return 0;
    }
}

static void doom_queue_key(int pressed, int scancode) {
    unsigned int next;
    unsigned char key = doom_scancode_to_key(scancode);

    if (key == 0) return;
    next = (doom_state.write_idx + 1U) % DOOM_KEYQUEUE_SIZE;
    if (next == doom_state.read_idx) {
        doom_state.read_idx = (doom_state.read_idx + 1U) % DOOM_KEYQUEUE_SIZE;
    }
    doom_state.events[doom_state.write_idx] = (unsigned short)(((pressed != 0) << 8) | key);
    doom_state.write_idx = next;
}

static void doom_poll_events(void) {
    gui_window_event_t event;

    while (user_gui_poll_event(doom_state.window_id, &event) > 0) {
        if (event.type == GUI_WIN_EVT_CLOSE_REQUEST) {
            doom_state.running = 0;
        } else if (event.type == GUI_WIN_EVT_KEY_DOWN) {
            doom_queue_key(1, event.arg0);
        } else if (event.type == GUI_WIN_EVT_KEY_UP) {
            doom_queue_key(0, event.arg0);
        } else if (event.type == GUI_WIN_EVT_WINDOW_RESIZED) {
            doom_state.width = 0;
            doom_state.height = 0;
        }
    }
}

static int doom_ensure_present_surface(void) {
    gui_window_info_t info;
    uint64_t pixels64;
    uint32_t pixels;

    if (user_gui_get_window_info(doom_state.window_id, &info) != 0) return -1;
    doom_state.width = info.client_width > 0 ? info.client_width : DOOMGENERIC_RESX;
    doom_state.height = info.client_height > 0 ? info.client_height : DOOMGENERIC_RESY;
    if (doom_state.width <= 0 || doom_state.height <= 0) return -1;
    pixels64 = (uint64_t)(uint32_t)doom_state.width * (uint64_t)(uint32_t)doom_state.height;
    if (pixels64 == 0U || pixels64 > 0x3FFFFFU) return -1;
    if (doom_state.width >= DOOMGENERIC_RESX && doom_state.height >= DOOMGENERIC_RESY) return 0;
    pixels = (uint32_t)pixels64;
    if (!doom_state.present_pixels || pixels > doom_state.present_capacity) {
        uint32_t* new_pixels = (uint32_t*)user_malloc((size_t)pixels * sizeof(uint32_t));

        if (!new_pixels) return -1;
        if (doom_state.present_pixels) user_free(doom_state.present_pixels);
        doom_state.present_pixels = new_pixels;
        doom_state.present_capacity = pixels;
    }
    return 0;
}

static int doom_ensure_x_map(void) {
    uint32_t x_step;
    uint32_t sx_acc = 0;

    if (doom_state.width <= 0) return -1;
    if (doom_state.x_map && doom_state.x_map_width == doom_state.width) return 0;
    if (!doom_state.x_map || (uint32_t)doom_state.width > doom_state.x_map_capacity) {
        uint32_t cap = (uint32_t)doom_state.width;
        uint16_t* next = (uint16_t*)user_malloc((size_t)cap * sizeof(uint16_t));

        if (!next) return -1;
        if (doom_state.x_map) user_free(doom_state.x_map);
        doom_state.x_map = next;
        doom_state.x_map_capacity = cap;
    }

    x_step = ((uint32_t)DOOMGENERIC_RESX << 16) / (uint32_t)doom_state.width;
    for (int x = 0; x < doom_state.width; x++) {
        uint32_t sx = sx_acc >> 16;

        if (sx >= (uint32_t)DOOMGENERIC_RESX) sx = (uint32_t)DOOMGENERIC_RESX - 1U;
        doom_state.x_map[x] = (uint16_t)sx;
        sx_acc += x_step;
    }
    doom_state.x_map_width = doom_state.width;
    return 0;
}

static void doom_copy_pixels(uint32_t* dst, const uint32_t* src, int count) {
    int x = 0;

    for (; x + 7 < count; x += 8) {
        dst[x + 0] = src[x + 0];
        dst[x + 1] = src[x + 1];
        dst[x + 2] = src[x + 2];
        dst[x + 3] = src[x + 3];
        dst[x + 4] = src[x + 4];
        dst[x + 5] = src[x + 5];
        dst[x + 6] = src[x + 6];
        dst[x + 7] = src[x + 7];
    }
    for (; x < count; x++) dst[x] = src[x];
}

static void doom_expand_row_2x(uint32_t* dst0, uint32_t* dst1, const uint32_t* src) {
    for (int x = 0; x < DOOMGENERIC_RESX; x++) {
        uint32_t c = src[x];
        int dx = x * 2;

        dst0[dx] = c;
        dst0[dx + 1] = c;
        dst1[dx] = c;
        dst1[dx + 1] = c;
    }
}

static void doom_expand_row_3x(uint32_t* dst0, uint32_t* dst1, uint32_t* dst2, const uint32_t* src) {
    for (int x = 0; x < DOOMGENERIC_RESX; x++) {
        uint32_t c = src[x];
        int dx = x * 3;

        dst0[dx] = c;
        dst0[dx + 1] = c;
        dst0[dx + 2] = c;
        dst1[dx] = c;
        dst1[dx + 1] = c;
        dst1[dx + 2] = c;
        dst2[dx] = c;
        dst2[dx + 1] = c;
        dst2[dx + 2] = c;
    }
}

static void doom_expand_row_4x(uint32_t* dst0, uint32_t* dst1, uint32_t* dst2, uint32_t* dst3,
                               const uint32_t* src) {
    for (int x = 0; x < DOOMGENERIC_RESX; x++) {
        uint32_t c = src[x];
        int dx = x * 4;

        dst0[dx] = c;
        dst0[dx + 1] = c;
        dst0[dx + 2] = c;
        dst0[dx + 3] = c;
        dst1[dx] = c;
        dst1[dx + 1] = c;
        dst1[dx + 2] = c;
        dst1[dx + 3] = c;
        dst2[dx] = c;
        dst2[dx + 1] = c;
        dst2[dx + 2] = c;
        dst2[dx + 3] = c;
        dst3[dx] = c;
        dst3[dx + 1] = c;
        dst3[dx + 2] = c;
        dst3[dx + 3] = c;
    }
}

static int doom_integer_scale_factor(void) {
    if (doom_state.width % DOOMGENERIC_RESX != 0 || doom_state.height % DOOMGENERIC_RESY != 0) return 0;
    int sx = doom_state.width / DOOMGENERIC_RESX;
    int sy = doom_state.height / DOOMGENERIC_RESY;

    if (sx != sy) return 0;
    return sx >= 2 && sx <= 4 ? sx : 0;
}

static void doom_scale_frame_integer(int scale) {
    for (int y = 0; y < DOOMGENERIC_RESY; y++) {
        const uint32_t* src = (const uint32_t*)DG_ScreenBuffer + (size_t)y * DOOMGENERIC_RESX;
        uint32_t* dst = doom_state.present_pixels + (size_t)y * (size_t)scale * (size_t)doom_state.width;

        if (scale == 2) {
            doom_expand_row_2x(dst, dst + doom_state.width, src);
        } else if (scale == 3) {
            doom_expand_row_3x(dst, dst + doom_state.width, dst + doom_state.width * 2, src);
        } else {
            doom_expand_row_4x(dst, dst + doom_state.width, dst + doom_state.width * 2,
                               dst + doom_state.width * 3, src);
        }
    }
}

static int doom_scale_frame_generic(void) {
    uint32_t y_step;
    uint32_t y_acc = 0;
    int prev_sy = -1;
    uint32_t* prev_dst = 0;

    if (doom_state.width == DOOMGENERIC_RESX && doom_state.height == DOOMGENERIC_RESY) {
        return 0;
    }
    if (doom_ensure_x_map() != 0) return -1;

    y_step = ((uint32_t)DOOMGENERIC_RESY << 16) / (uint32_t)doom_state.height;
    for (int y = 0; y < doom_state.height; y++) {
        int sy = (int)(y_acc >> 16);
        uint32_t* dst = doom_state.present_pixels + (size_t)y * (size_t)doom_state.width;

        if (sy >= DOOMGENERIC_RESY) sy = DOOMGENERIC_RESY - 1;
        if (sy == prev_sy && prev_dst) {
            doom_copy_pixels(dst, prev_dst, doom_state.width);
        } else {
            const uint32_t* src = (const uint32_t*)DG_ScreenBuffer + (size_t)sy * DOOMGENERIC_RESX;
            int x = 0;

            for (; x + 3 < doom_state.width; x += 4) {
                dst[x + 0] = src[doom_state.x_map[x + 0]];
                dst[x + 1] = src[doom_state.x_map[x + 1]];
                dst[x + 2] = src[doom_state.x_map[x + 2]];
                dst[x + 3] = src[doom_state.x_map[x + 3]];
            }
            for (; x < doom_state.width; x++) {
                dst[x] = src[doom_state.x_map[x]];
            }
            prev_sy = sy;
            prev_dst = dst;
        }
        y_acc += y_step;
    }
    return 0;
}

static int doom_scale_frame(void) {
    int scale = doom_integer_scale_factor();

    if (scale != 0) {
        doom_scale_frame_integer(scale);
        return 0;
    }
    return doom_scale_frame_generic();
}

void DG_Init(void) {
    gui_create_window_params_t params;

    params.size = sizeof(params);
    params.flags = 0;
    params.x = -1;
    params.y = -1;
    params.width = DOOMGENERIC_RESX * 2 + 2;
    params.height = DOOMGENERIC_RESY * 2 + 42;
    doom_state.window_id = user_gui_create_window(&params);
    if (doom_state.window_id < 0) {
        userlib_print_error("doom: failed to create GUI window");
        user_exit(1);
    }
    doom_state.running = 1;
    (void)user_gui_set_title(doom_state.window_id, "Doom");
    if (doom_ensure_present_surface() != 0) {
        userlib_print_error("doom: failed to allocate framebuffer");
        user_exit(1);
    }
}

void DG_DrawFrame(void) {
    gui_present_params_t present;
    uintptr_t buffer_ptr;
    uint32_t stride_bytes;
    uint32_t flags = 0;

    doom_poll_events();
    if (!doom_state.running) user_exit(0);
    if (doom_ensure_present_surface() != 0) user_exit(1);

    if (doom_state.width == DOOMGENERIC_RESX && doom_state.height == DOOMGENERIC_RESY) {
        buffer_ptr = (uintptr_t)DG_ScreenBuffer;
        stride_bytes = DOOMGENERIC_RESX * 4U;
    } else if (doom_state.width >= DOOMGENERIC_RESX && doom_state.height >= DOOMGENERIC_RESY) {
        buffer_ptr = (uintptr_t)DG_ScreenBuffer;
        stride_bytes = DOOMGENERIC_RESX * 4U;
        flags = GUI_PRESENT_FLAG_STRETCH_CLIENT;
    } else {
        if (doom_scale_frame() != 0) user_exit(1);
        buffer_ptr = (uintptr_t)doom_state.present_pixels;
        stride_bytes = (uint32_t)doom_state.width * 4U;
    }

    present.size = sizeof(present);
    present.flags = flags;
    present.buffer_ptr = buffer_ptr;
    present.x = 0;
    present.y = 0;
    present.width = flags == GUI_PRESENT_FLAG_STRETCH_CLIENT ? DOOMGENERIC_RESX : (uint32_t)doom_state.width;
    present.height = flags == GUI_PRESENT_FLAG_STRETCH_CLIENT ? DOOMGENERIC_RESY : (uint32_t)doom_state.height;
    present.stride_bytes = stride_bytes;
    if (user_gui_present(doom_state.window_id, &present) != 0) user_exit(1);
}

void DG_SleepMs(uint32_t ms) {
    uint32_t ticks = (ms + 9U) / 10U;

    if (ticks == 0U) {
        user_yield();
        return;
    }
    (void)user_sleep(ticks);
}

uint32_t DG_GetTicksMs(void) {
    return user_uptime_ticks() * 10U;
}

int DG_GetKey(int* pressed, unsigned char* doomKey) {
    unsigned short event;

    doom_poll_events();
    if (doom_state.read_idx == doom_state.write_idx) return 0;
    event = doom_state.events[doom_state.read_idx];
    doom_state.read_idx = (doom_state.read_idx + 1U) % DOOM_KEYQUEUE_SIZE;
    *pressed = event >> 8;
    *doomKey = (unsigned char)(event & 0xFFU);
    return 1;
}

void DG_SetWindowTitle(const char* title) {
    if (doom_state.window_id >= 0) (void)user_gui_set_title(doom_state.window_id, title ? title : "Doom");
}

int main(int argc, char** argv) {
    char* doom_argv[DOOM_MAX_ARGS];
    int doom_argc = 0;

    (void)user_fs_mkdir(DOOM_CONFIG_DIR);
    if (user_fs_find_node(DOOM_DEFAULT_WAD) < 0) {
        userlib_print_error("doom: required IWAD missing: /assets/doom1.wad");
        return 1;
    }

    doom_argv[doom_argc++] = argc > 0 && argv && argv[0] ? argv[0] : "doom";
    if (!doom_has_arg(argc, argv, "-iwad")) {
        doom_argv[doom_argc++] = "-iwad";
        doom_argv[doom_argc++] = DOOM_DEFAULT_WAD;
    }
    if (!doom_has_arg(argc, argv, "-config")) {
        doom_argv[doom_argc++] = "-config";
        doom_argv[doom_argc++] = DOOM_CONFIG_FILE;
    }
    if (!doom_has_arg(argc, argv, "-nosound")) {
        doom_argv[doom_argc++] = "-nosound";
    }
    for (int i = 1; i < argc && doom_argc < (int)(sizeof(doom_argv) / sizeof(doom_argv[0])); i++) {
        doom_argv[doom_argc++] = argv[i];
    }

    doomgeneric_Create(doom_argc, doom_argv);
    while (doom_state.running) {
        doomgeneric_Tick();
        user_yield();
    }
    return 0;
}
