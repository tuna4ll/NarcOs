#include "usermode.h"
#include "paging.h"
#include "process.h"
#include "string.h"
#include "vbe.h"

extern volatile int gui_needs_redraw;
extern window_t windows[MAX_WINDOWS];
extern int active_window_idx;
extern int nwm_get_idx_by_type(window_type_t type);
extern void user_snake_entry_gate();
extern void user_netdemo_entry_gate();
extern void user_fetch_entry_gate();
extern void user_shell_entry_gate();
extern void user_narcpad_entry_gate();
extern void user_settings_entry_gate();
extern void user_explorer_entry_gate();
extern void vga_print(const char* str);
extern void vga_println(const char* str);
extern void vga_print_color(const char* str, uint8_t color);
extern void vga_print_int(int num);

typedef enum {
    USER_TASK_NONE = 0,
    USER_TASK_SNAKE,
    USER_TASK_NETDEMO,
    USER_TASK_FETCH,
    USER_TASK_SHELL,
    USER_TASK_NARCPAD,
    USER_TASK_SETTINGS,
    USER_TASK_EXPLORER
} user_task_kind_t;

static trap_frame_t snake_context;
static trap_frame_t netdemo_context;
static trap_frame_t fetch_context;
static trap_frame_t shell_context;
static trap_frame_t narcpad_context;
static trap_frame_t settings_context;
static trap_frame_t explorer_context;
static user_task_kind_t active_user_task = USER_TASK_NONE;
static int snake_best_persist = 0;
static volatile int snake_input_pending = -1;
static volatile int snake_running = 0;
static volatile int narcpad_running = 0;
static volatile int settings_running = 0;
static volatile int explorer_running = 0;
static int snake_last_head_x = -1;
static int snake_last_head_y = -1;
static int snake_last_len = -1;
static int snake_last_apple_x = -1;
static int snake_last_apple_y = -1;
static int snake_last_dead = -1;
static int snake_last_score = -1;
static int snake_last_best = -1;

#define USER_PAGE_SIZE              4096U
#define USER_SNAKE_STATE_PAGES      1U
#define USER_SNAKE_STACK_PAGES      1U
#define USER_NETDEMO_STATE_PAGES    1U
#define USER_NETDEMO_STACK_PAGES    1U
#define USER_FETCH_STATE_PAGES      2U
#define USER_FETCH_STACK_PAGES      1U
#define USER_SHELL_STATE_PAGES      4U
#define USER_SHELL_STACK_PAGES      1U
#define USER_NARCPAD_STATE_PAGES    1U
#define USER_NARCPAD_STACK_PAGES    1U
#define USER_SETTINGS_STATE_PAGES   1U
#define USER_SETTINGS_STACK_PAGES   1U
#define USER_EXPLORER_STATE_PAGES   1U
#define USER_EXPLORER_STACK_PAGES   1U
#define USER_SNAKE_STATE_VA         (USER_DATA_WINDOW_BASE + 0x0000U)
#define USER_SNAKE_STACK_VA         (USER_DATA_WINDOW_BASE + 0x1000U)
#define USER_NETDEMO_STATE_VA       (USER_DATA_WINDOW_BASE + 0x2000U)
#define USER_NETDEMO_STACK_VA       (USER_DATA_WINDOW_BASE + 0x3000U)
#define USER_FETCH_STATE_VA         (USER_DATA_WINDOW_BASE + 0x4000U)
#define USER_FETCH_STACK_VA         (USER_DATA_WINDOW_BASE + 0x6000U)
#define USER_SHELL_STATE_VA         (USER_DATA_WINDOW_BASE + 0x7000U)
#define USER_SHELL_STACK_VA         (USER_DATA_WINDOW_BASE + 0xB000U)
#define USER_NARCPAD_STATE_VA       (USER_DATA_WINDOW_BASE + 0xC000U)
#define USER_NARCPAD_STACK_VA       (USER_DATA_WINDOW_BASE + 0xD000U)
#define USER_SETTINGS_STATE_VA      (USER_DATA_WINDOW_BASE + 0xE000U)
#define USER_SETTINGS_STACK_VA      (USER_DATA_WINDOW_BASE + 0xF000U)
#define USER_EXPLORER_STATE_VA      (USER_DATA_WINDOW_BASE + 0x10000U)
#define USER_EXPLORER_STACK_VA      (USER_DATA_WINDOW_BASE + 0x11000U)

static uint8_t snake_state_region[USER_SNAKE_STATE_PAGES * USER_PAGE_SIZE] __attribute__((aligned(USER_PAGE_SIZE)));
static uint8_t snake_stack_region[USER_SNAKE_STACK_PAGES * USER_PAGE_SIZE] __attribute__((aligned(USER_PAGE_SIZE)));
static uint8_t netdemo_state_region[USER_NETDEMO_STATE_PAGES * USER_PAGE_SIZE] __attribute__((aligned(USER_PAGE_SIZE)));
static uint8_t netdemo_stack_region[USER_NETDEMO_STACK_PAGES * USER_PAGE_SIZE] __attribute__((aligned(USER_PAGE_SIZE)));
static uint8_t fetch_state_region[USER_FETCH_STATE_PAGES * USER_PAGE_SIZE] __attribute__((aligned(USER_PAGE_SIZE)));
static uint8_t fetch_stack_region[USER_FETCH_STACK_PAGES * USER_PAGE_SIZE] __attribute__((aligned(USER_PAGE_SIZE)));
static uint8_t shell_state_region[USER_SHELL_STATE_PAGES * USER_PAGE_SIZE] __attribute__((aligned(USER_PAGE_SIZE)));
static uint8_t shell_stack_region[USER_SHELL_STACK_PAGES * USER_PAGE_SIZE] __attribute__((aligned(USER_PAGE_SIZE)));
static uint8_t narcpad_state_region[USER_NARCPAD_STATE_PAGES * USER_PAGE_SIZE] __attribute__((aligned(USER_PAGE_SIZE)));
static uint8_t narcpad_stack_region[USER_NARCPAD_STACK_PAGES * USER_PAGE_SIZE] __attribute__((aligned(USER_PAGE_SIZE)));
static uint8_t settings_state_region[USER_SETTINGS_STATE_PAGES * USER_PAGE_SIZE] __attribute__((aligned(USER_PAGE_SIZE)));
static uint8_t settings_stack_region[USER_SETTINGS_STACK_PAGES * USER_PAGE_SIZE] __attribute__((aligned(USER_PAGE_SIZE)));
static uint8_t explorer_state_region[USER_EXPLORER_STATE_PAGES * USER_PAGE_SIZE] __attribute__((aligned(USER_PAGE_SIZE)));
static uint8_t explorer_stack_region[USER_EXPLORER_STACK_PAGES * USER_PAGE_SIZE] __attribute__((aligned(USER_PAGE_SIZE)));
static int user_memory_ready = 0;

user_snake_state_t* user_snake_state_ptr = (user_snake_state_t*)snake_state_region;
user_netdemo_state_t* user_netdemo_state_ptr = (user_netdemo_state_t*)netdemo_state_region;
user_fetch_state_t* user_fetch_state_ptr = (user_fetch_state_t*)fetch_state_region;
user_shell_state_t* user_shell_state_ptr = (user_shell_state_t*)shell_state_region;
user_narcpad_state_t* user_narcpad_state_ptr = (user_narcpad_state_t*)narcpad_state_region;
user_settings_state_t* user_settings_state_ptr = (user_settings_state_t*)settings_state_region;
user_explorer_state_t* user_explorer_state_ptr = (user_explorer_state_t*)explorer_state_region;
uint32_t user_kernel_resume_esp = 0;
uint32_t user_kernel_ebx = 0;
uint32_t user_kernel_esi = 0;
uint32_t user_kernel_edi = 0;
uint32_t user_kernel_ebp = 0;

static void reset_user_snake_state() {
    memset(&user_snake_state, 0, sizeof(user_snake_state));
    user_snake_state.best = snake_best_persist;
}

static void invalidate_snake_frame_cache() {
    snake_last_head_x = -1;
    snake_last_head_y = -1;
    snake_last_len = -1;
    snake_last_apple_x = -1;
    snake_last_apple_y = -1;
    snake_last_dead = -1;
    snake_last_score = -1;
    snake_last_best = -1;
}

static int snake_frame_changed() {
    return user_snake_state.px[0] != snake_last_head_x ||
           user_snake_state.py[0] != snake_last_head_y ||
           user_snake_state.len != snake_last_len ||
           user_snake_state.apple_x != snake_last_apple_x ||
           user_snake_state.apple_y != snake_last_apple_y ||
           user_snake_state.dead != snake_last_dead ||
           user_snake_state.score != snake_last_score ||
           user_snake_state.best != snake_last_best;
}

static void remember_snake_frame() {
    snake_last_head_x = user_snake_state.px[0];
    snake_last_head_y = user_snake_state.py[0];
    snake_last_len = user_snake_state.len;
    snake_last_apple_x = user_snake_state.apple_x;
    snake_last_apple_y = user_snake_state.apple_y;
    snake_last_dead = user_snake_state.dead;
    snake_last_score = user_snake_state.score;
    snake_last_best = user_snake_state.best;
}

static void reset_user_narcpad_state() {
    memset(&user_narcpad_state, 0, sizeof(user_narcpad_state));
    strncpy(user_narcpad_state.title, "untitled.txt", sizeof(user_narcpad_state.title) - 1U);
    user_narcpad_state.title[sizeof(user_narcpad_state.title) - 1U] = '\0';
}

static void reset_user_settings_state() {
    memset(&user_settings_state, 0, sizeof(user_settings_state));
}

static void reset_user_explorer_state(int initial_dir) {
    memset(&user_explorer_state, 0, sizeof(user_explorer_state));
    user_explorer_state.current_dir = initial_dir;
    user_explorer_state.prev_dir = -1;
    user_explorer_state.selected_idx = -1;
}

static void sync_narcpad_window_title() {
    int idx = nwm_get_idx_by_type(WIN_TYPE_NARCPAD);

    if (idx == -1) return;
    strncpy(windows[idx].title, user_narcpad_state.title, sizeof(windows[idx].title) - 1U);
    windows[idx].title[sizeof(windows[idx].title) - 1U] = '\0';
}

static int queue_user_event(uint16_t* types, int32_t* args, int cap,
                            int* head, int* tail, int type, int value) {
    int next_tail;

    if (!types || !args || !head || !tail || cap <= 1) return -1;
    next_tail = (*tail + 1) % cap;
    if (next_tail == *head) return -1;
    types[*tail] = (uint16_t)type;
    args[*tail] = (int32_t)value;
    *tail = next_tail;
    return 0;
}

static int init_user_memory_layout() {
    if (user_memory_ready) return 0;
    if (paging_map_user_region(USER_SNAKE_STATE_VA, (uint32_t)snake_state_region,
                               sizeof(snake_state_region), PAGING_FLAG_WRITE) != 0) return -1;
    if (paging_map_user_region(USER_SNAKE_STACK_VA, (uint32_t)snake_stack_region,
                               sizeof(snake_stack_region), PAGING_FLAG_WRITE) != 0) return -1;
    if (paging_map_user_region(USER_NETDEMO_STATE_VA, (uint32_t)netdemo_state_region,
                               sizeof(netdemo_state_region), PAGING_FLAG_WRITE) != 0) return -1;
    if (paging_map_user_region(USER_NETDEMO_STACK_VA, (uint32_t)netdemo_stack_region,
                               sizeof(netdemo_stack_region), PAGING_FLAG_WRITE) != 0) return -1;
    if (paging_map_user_region(USER_FETCH_STATE_VA, (uint32_t)fetch_state_region,
                               sizeof(fetch_state_region), PAGING_FLAG_WRITE) != 0) return -1;
    if (paging_map_user_region(USER_FETCH_STACK_VA, (uint32_t)fetch_stack_region,
                               sizeof(fetch_stack_region), PAGING_FLAG_WRITE) != 0) return -1;
    if (paging_map_user_region(USER_SHELL_STATE_VA, (uint32_t)shell_state_region,
                               sizeof(shell_state_region), PAGING_FLAG_WRITE) != 0) return -1;
    if (paging_map_user_region(USER_SHELL_STACK_VA, (uint32_t)shell_stack_region,
                               sizeof(shell_stack_region), PAGING_FLAG_WRITE) != 0) return -1;
    if (paging_map_user_region(USER_NARCPAD_STATE_VA, (uint32_t)narcpad_state_region,
                               sizeof(narcpad_state_region), PAGING_FLAG_WRITE) != 0) return -1;
    if (paging_map_user_region(USER_NARCPAD_STACK_VA, (uint32_t)narcpad_stack_region,
                               sizeof(narcpad_stack_region), PAGING_FLAG_WRITE) != 0) return -1;
    if (paging_map_user_region(USER_SETTINGS_STATE_VA, (uint32_t)settings_state_region,
                               sizeof(settings_state_region), PAGING_FLAG_WRITE) != 0) return -1;
    if (paging_map_user_region(USER_SETTINGS_STACK_VA, (uint32_t)settings_stack_region,
                               sizeof(settings_stack_region), PAGING_FLAG_WRITE) != 0) return -1;
    if (paging_map_user_region(USER_EXPLORER_STATE_VA, (uint32_t)explorer_state_region,
                               sizeof(explorer_state_region), PAGING_FLAG_WRITE) != 0) return -1;
    if (paging_map_user_region(USER_EXPLORER_STACK_VA, (uint32_t)explorer_stack_region,
                               sizeof(explorer_stack_region), PAGING_FLAG_WRITE) != 0) return -1;
    user_memory_ready = 1;
    return 0;
}

static void init_user_context(trap_frame_t* context, uint32_t user_stack_base, uint32_t user_stack_pages,
                              uint32_t entry_point, uint32_t user_state_ptr) {
    memset(context, 0, sizeof(*context));
    context->gs = USER_DATA_SEG;
    context->fs = USER_DATA_SEG;
    context->es = USER_DATA_SEG;
    context->ds = USER_DATA_SEG;
    context->edi = user_state_ptr;
    context->eip = entry_point;
    context->cs = USER_CODE_SEG;
    context->eflags = 0x202;
    context->user_esp = user_stack_base + user_stack_pages * USER_PAGE_SIZE - 16U;
    context->user_ss = USER_DATA_SEG;
}

static int parse_http_target(const char* target, char* host, int host_len, char* path, int path_len) {
    const char* cursor = target;
    int host_off = 0;
    int path_off = 0;

    if (!target || !host || !path || host_len <= 1 || path_len <= 1) return -1;
    while (*cursor == ' ') cursor++;
    if (strncmp(cursor, "http://", 7) == 0) cursor += 7;

    while (*cursor && *cursor != ' ' && *cursor != '/') {
        if (host_off + 1 >= host_len) return -1;
        host[host_off++] = *cursor++;
    }
    host[host_off] = '\0';
    if (host_off == 0) return -1;

    if (*cursor == '/') {
        while (*cursor && *cursor != ' ') {
            if (path_off + 1 >= path_len) return -1;
            path[path_off++] = *cursor++;
        }
    } else {
        while (*cursor == ' ') cursor++;
        if (*cursor != '\0') {
            if (*cursor != '/') {
                if (path_off + 1 >= path_len) return -1;
                path[path_off++] = '/';
            }
            while (*cursor) {
                if (path_off + 1 >= path_len) return -1;
                path[path_off++] = *cursor++;
            }
        }
    }

    if (path_off == 0) path[path_off++] = '/';
    path[path_off] = '\0';
    return 0;
}

static int parse_host_only(const char* text, char* host, int host_len) {
    const char* cursor = text;
    int off = 0;

    if (!text || !host || host_len <= 1) return -1;
    while (*cursor == ' ') cursor++;
    if (strncmp(cursor, "http://", 7) == 0) cursor += 7;
    while (*cursor && *cursor != ' ' && *cursor != '/') {
        if (off + 1 >= host_len) return -1;
        host[off++] = *cursor++;
    }
    host[off] = '\0';
    return off != 0 ? 0 : -1;
}

static int next_arg_token(const char* src, int* io_off, char* out, int out_len) {
    int off = *io_off;
    int len = 0;

    if (!src || !io_off || !out || out_len <= 1) return -1;
    while (src[off] == ' ') off++;
    if (src[off] == '\0') return -1;

    while (src[off] != '\0' && src[off] != ' ') {
        if (len + 1 >= out_len) return -1;
        out[len++] = src[off++];
    }
    out[len] = '\0';
    *io_off = off;
    return 0;
}

static int parse_fetch_args(const char* args, char* host, int host_len,
                            char* path, int path_len, char* output_path, int output_path_len) {
    char token0[128];
    char token1[128];
    char token2[128];
    int off = 0;
    int token_count = 0;

    token0[0] = '\0';
    token1[0] = '\0';
    token2[0] = '\0';
    if (next_arg_token(args, &off, token0, sizeof(token0)) != 0) return -1;
    token_count++;
    if (next_arg_token(args, &off, token1, sizeof(token1)) != 0) return -1;
    token_count++;
    if (next_arg_token(args, &off, token2, sizeof(token2)) == 0) token_count++;
    while (args[off] == ' ') off++;
    if (args[off] != '\0') return -1;

    if (token_count == 2) {
        if (parse_http_target(token0, host, host_len, path, path_len) != 0) return -1;
        strncpy(output_path, token1, (size_t)(output_path_len - 1));
        output_path[output_path_len - 1] = '\0';
        return 0;
    }
    if (token_count == 3) {
        if (parse_host_only(token0, host, host_len) != 0) return -1;
        strncpy(path, token1, (size_t)(path_len - 1));
        path[path_len - 1] = '\0';
        strncpy(output_path, token2, (size_t)(output_path_len - 1));
        output_path[output_path_len - 1] = '\0';
        if (path[0] != '/') return -1;
        return 0;
    }
    return -1;
}

static void dispatch_user_task(user_task_kind_t kind, trap_frame_t* context) {
    process_t* current = process_current();
    uint32_t resume_stack_top = current ? current->kernel_stack_top : KERNEL_BOOT_STACK_TOP;

    active_user_task = kind;
    /* User traps must land on a clean kernel stack; otherwise INT frames overwrite
       the suspended process stack frame that run_user_task will later resume. */
    set_tss_stack(KERNEL_BOOT_STACK_TOP);
    run_user_task(context);
    set_tss_stack(resume_stack_top);
    active_user_task = USER_TASK_NONE;
}

static int run_sync_user_app(user_task_kind_t kind, trap_frame_t* context, int* status_ptr) {
    while (*status_ptr == USER_APP_STATUS_RUNNING) {
        dispatch_user_task(kind, context);
    }
    return *status_ptr;
}

int init_usermode() {
    user_memory_ready = 0;
    memset(&snake_context, 0, sizeof(snake_context));
    memset(&netdemo_context, 0, sizeof(netdemo_context));
    memset(&fetch_context, 0, sizeof(fetch_context));
    memset(&shell_context, 0, sizeof(shell_context));
    memset(&narcpad_context, 0, sizeof(narcpad_context));
    memset(&settings_context, 0, sizeof(settings_context));
    memset(&explorer_context, 0, sizeof(explorer_context));
    reset_user_snake_state();
    memset(&user_netdemo_state, 0, sizeof(user_netdemo_state));
    memset(&user_fetch_state, 0, sizeof(user_fetch_state));
    memset(&user_shell_state, 0, sizeof(user_shell_state));
    reset_user_narcpad_state();
    reset_user_settings_state();
    reset_user_explorer_state(-1);
    invalidate_snake_frame_cache();
    snake_input_pending = -1;
    snake_running = 0;
    narcpad_running = 0;
    settings_running = 0;
    explorer_running = 0;
    active_user_task = USER_TASK_NONE;
    if (init_user_memory_layout() != 0) return -1;
    return 0;
}

void launch_user_snake() {
    int sidx = nwm_get_idx_by_type(WIN_TYPE_SNAKE);
    if (sidx != -1) {
        windows[sidx].visible = 1;
        windows[sidx].minimized = 0;
        active_window_idx = sidx;
    }

    reset_user_snake_state();
    invalidate_snake_frame_cache();
    init_user_context(&snake_context, USER_SNAKE_STACK_VA, USER_SNAKE_STACK_PAGES,
                      (uint32_t)user_snake_entry_gate, USER_SNAKE_STATE_VA);

    snake_input_pending = -1;
    snake_running = 1;
    gui_needs_redraw = 1;
}

void launch_user_narcpad() {
    int idx = nwm_get_idx_by_type(WIN_TYPE_NARCPAD);

    if (idx != -1) {
        windows[idx].visible = 1;
        windows[idx].minimized = 0;
        active_window_idx = idx;
    }
    if (narcpad_running) return;

    reset_user_narcpad_state();
    init_user_context(&narcpad_context, USER_NARCPAD_STACK_VA, USER_NARCPAD_STACK_PAGES,
                      (uint32_t)user_narcpad_entry_gate, USER_NARCPAD_STATE_VA);
    user_narcpad_state.status = USER_APP_STATUS_RUNNING;
    narcpad_running = 1;
    sync_narcpad_window_title();
    gui_needs_redraw = 1;
}

void launch_user_settings() {
    int idx = nwm_get_idx_by_type(WIN_TYPE_SETTINGS);

    if (idx != -1) {
        windows[idx].visible = 1;
        windows[idx].minimized = 0;
        active_window_idx = idx;
    }
    if (settings_running) return;

    reset_user_settings_state();
    init_user_context(&settings_context, USER_SETTINGS_STACK_VA, USER_SETTINGS_STACK_PAGES,
                      (uint32_t)user_settings_entry_gate, USER_SETTINGS_STATE_VA);
    user_settings_state.status = USER_APP_STATUS_RUNNING;
    settings_running = 1;
    gui_needs_redraw = 1;
}

void launch_user_explorer(int initial_dir) {
    int idx = nwm_get_idx_by_type(WIN_TYPE_EXPLORER);

    if (idx != -1) {
        windows[idx].visible = 1;
        windows[idx].minimized = 0;
        active_window_idx = idx;
    }
    if (explorer_running) return;

    reset_user_explorer_state(initial_dir);
    init_user_context(&explorer_context, USER_EXPLORER_STACK_VA, USER_EXPLORER_STACK_PAGES,
                      (uint32_t)user_explorer_entry_gate, USER_EXPLORER_STATE_VA);
    user_explorer_state.status = USER_APP_STATUS_RUNNING;
    explorer_running = 1;
    gui_needs_redraw = 1;
}

void run_user_tasks() {
    if (snake_running) {
        dispatch_user_task(USER_TASK_SNAKE, &snake_context);
    }
    if (explorer_running) {
        dispatch_user_task(USER_TASK_EXPLORER, &explorer_context);
    }
    if (settings_running) {
        dispatch_user_task(USER_TASK_SETTINGS, &settings_context);
    }
    if (narcpad_running) {
        dispatch_user_task(USER_TASK_NARCPAD, &narcpad_context);
    }
}

int run_user_netdemo(const char* target) {
    const char* resolved_target = (target && target[0] != '\0') ? target : "example.com /";
    int status;

    memset(&user_netdemo_state, 0, sizeof(user_netdemo_state));
    if (parse_http_target(resolved_target,
                          user_netdemo_state.host, sizeof(user_netdemo_state.host),
                          user_netdemo_state.path, sizeof(user_netdemo_state.path)) != 0) {
        vga_print_color("Usage: netdemo <host> [path]\n", 0x0E);
        return -1;
    }

    user_netdemo_state.status = USER_APP_STATUS_RUNNING;
    init_user_context(&netdemo_context, USER_NETDEMO_STACK_VA, USER_NETDEMO_STACK_PAGES,
                      (uint32_t)user_netdemo_entry_gate, USER_NETDEMO_STATE_VA);
    status = run_sync_user_app(USER_TASK_NETDEMO, &netdemo_context, &user_netdemo_state.status);
    if (status < 0) {
        vga_print_color("netdemo: ", 0x0C);
        vga_println(net_strerror(status));
    }
    return status;
}

int run_user_fetch(const char* args) {
    int status;

    memset(&user_fetch_state, 0, sizeof(user_fetch_state));
    if (parse_fetch_args(args,
                         user_fetch_state.host, sizeof(user_fetch_state.host),
                         user_fetch_state.path, sizeof(user_fetch_state.path),
                         user_fetch_state.output_path, sizeof(user_fetch_state.output_path)) != 0) {
        vga_print_color("Usage: fetch <host> [path] <output-file>\n", 0x0E);
        return -1;
    }

    user_fetch_state.status = USER_APP_STATUS_RUNNING;
    init_user_context(&fetch_context, USER_FETCH_STACK_VA, USER_FETCH_STACK_PAGES,
                      (uint32_t)user_fetch_entry_gate, USER_FETCH_STATE_VA);
    status = run_sync_user_app(USER_TASK_FETCH, &fetch_context, &user_fetch_state.status);
    if (status < 0) {
        vga_print_color("fetch: ", 0x0C);
        vga_println(net_strerror(status));
        return status;
    }

    vga_print("Saved ");
    vga_print_int((int)user_fetch_state.saved_len);
    vga_print(" bytes to ");
    vga_println(user_fetch_state.output_path);
    if (user_fetch_state.result.truncated != 0U) {
        vga_print_color("warning: Response truncated to local fetch buffer.\n", 0x0E);
    }
    if (user_fetch_state.result.complete == 0U) {
        vga_print_color("warning: Remote peer did not close cleanly before timeout.\n", 0x0E);
    }
    return status;
}

int run_user_shell_command(const char* command) {
    int status;

    memset(&user_shell_state, 0, sizeof(user_shell_state));
    if (command) {
        strncpy(user_shell_state.command, command, sizeof(user_shell_state.command) - 1U);
        user_shell_state.command[sizeof(user_shell_state.command) - 1U] = '\0';
    }

    user_shell_state.status = USER_APP_STATUS_RUNNING;
    user_shell_state.exit_code = 0;
    init_user_context(&shell_context, USER_SHELL_STACK_VA, USER_SHELL_STACK_PAGES,
                      (uint32_t)user_shell_entry_gate, USER_SHELL_STATE_VA);
    status = run_sync_user_app(USER_TASK_SHELL, &shell_context, &user_shell_state.status);
    return status == USER_APP_STATUS_OK ? user_shell_state.exit_code : status;
}

void stop_user_snake() {
    int sidx = nwm_get_idx_by_type(WIN_TYPE_SNAKE);
    if (user_snake_state.best > snake_best_persist) {
        snake_best_persist = user_snake_state.best;
    }
    snake_running = 0;
    snake_input_pending = -1;
    if (sidx != -1) {
        windows[sidx].visible = 0;
        windows[sidx].minimized = 0;
        if (active_window_idx == sidx) active_window_idx = -1;
    }
    gui_needs_redraw = 1;
}

int user_snake_running() {
    return snake_running;
}

int user_narcpad_running() {
    return narcpad_running;
}

int user_settings_running() {
    return settings_running;
}

int user_explorer_running() {
    return explorer_running;
}

void queue_user_snake_input(int input) {
    snake_input_pending = input;
}

int consume_user_snake_input() {
    int input = snake_input_pending;
    snake_input_pending = -1;
    return input;
}

void queue_user_narcpad_event(int type, int value) {
    (void)queue_user_event(user_narcpad_state.event_type, user_narcpad_state.event_arg,
                           USER_GUI_EVENT_QUEUE_CAP, &user_narcpad_state.event_head,
                           &user_narcpad_state.event_tail, type, value);
}

void request_user_narcpad_new() {
    if (!narcpad_running) launch_user_narcpad();
    queue_user_narcpad_event(USER_NARCPAD_EVT_OPEN_NEW, 0);
}

void request_user_narcpad_open(const char* path) {
    if (!path || path[0] == '\0') return;
    if (!narcpad_running) launch_user_narcpad();
    strncpy(user_narcpad_state.request_path, path, sizeof(user_narcpad_state.request_path) - 1U);
    user_narcpad_state.request_path[sizeof(user_narcpad_state.request_path) - 1U] = '\0';
    queue_user_narcpad_event(USER_NARCPAD_EVT_OPEN_PATH, 0);
}

void queue_user_settings_event(int type, int value) {
    if (!settings_running) return;
    (void)queue_user_event(user_settings_state.event_type, user_settings_state.event_arg,
                           USER_GUI_EVENT_QUEUE_CAP, &user_settings_state.event_head,
                           &user_settings_state.event_tail, type, value);
}

void queue_user_explorer_event(int type, int value) {
    if (!explorer_running) return;
    (void)queue_user_event(user_explorer_state.event_type, user_explorer_state.event_arg,
                           USER_GUI_EVENT_QUEUE_CAP, &user_explorer_state.event_head,
                           &user_explorer_state.event_tail, type, value);
}

void user_yield_handler(trap_frame_t* frame) {
    if (active_user_task == USER_TASK_SNAKE) {
        int sidx = nwm_get_idx_by_type(WIN_TYPE_SNAKE);
        int should_redraw = snake_frame_changed();
        snake_context = *frame;
        remember_snake_frame();
        if (should_redraw && sidx != -1 && windows[sidx].visible && !windows[sidx].minimized) {
            gui_needs_redraw = 1;
        }
    } else if (active_user_task == USER_TASK_NETDEMO) {
        netdemo_context = *frame;
    } else if (active_user_task == USER_TASK_FETCH) {
        fetch_context = *frame;
    } else if (active_user_task == USER_TASK_SHELL) {
        shell_context = *frame;
    } else if (active_user_task == USER_TASK_NARCPAD) {
        int idx = nwm_get_idx_by_type(WIN_TYPE_NARCPAD);
        narcpad_context = *frame;
        sync_narcpad_window_title();
        if (user_narcpad_state.dirty != 0) {
            user_narcpad_state.dirty = 0;
            if (idx != -1 && windows[idx].visible && !windows[idx].minimized) gui_needs_redraw = 1;
        }
    } else if (active_user_task == USER_TASK_SETTINGS) {
        int idx = nwm_get_idx_by_type(WIN_TYPE_SETTINGS);
        settings_context = *frame;
        if (user_settings_state.dirty != 0) {
            user_settings_state.dirty = 0;
            if (idx != -1 && windows[idx].visible && !windows[idx].minimized) gui_needs_redraw = 1;
        }
    } else if (active_user_task == USER_TASK_EXPLORER) {
        int idx = nwm_get_idx_by_type(WIN_TYPE_EXPLORER);
        explorer_context = *frame;
        if (user_explorer_state.dirty != 0) {
            user_explorer_state.dirty = 0;
            if (idx != -1 && windows[idx].visible && !windows[idx].minimized) gui_needs_redraw = 1;
        }
    }
}
