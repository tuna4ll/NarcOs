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
extern void vga_print(const char* str);
extern void vga_println(const char* str);
extern void vga_print_color(const char* str, uint8_t color);
extern void vga_print_int(int num);

typedef enum {
    USER_TASK_NONE = 0,
    USER_TASK_SNAKE,
    USER_TASK_NETDEMO,
    USER_TASK_FETCH
} user_task_kind_t;

static trap_frame_t snake_context;
static trap_frame_t netdemo_context;
static trap_frame_t fetch_context;
static user_task_kind_t active_user_task = USER_TASK_NONE;
static int snake_best_persist = 0;
static volatile int snake_input_pending = -1;
static volatile int snake_running = 0;
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
#define USER_SNAKE_STATE_VA         (USER_DATA_WINDOW_BASE + 0x0000U)
#define USER_SNAKE_STACK_VA         (USER_DATA_WINDOW_BASE + 0x1000U)
#define USER_NETDEMO_STATE_VA       (USER_DATA_WINDOW_BASE + 0x2000U)
#define USER_NETDEMO_STACK_VA       (USER_DATA_WINDOW_BASE + 0x3000U)
#define USER_FETCH_STATE_VA         (USER_DATA_WINDOW_BASE + 0x4000U)
#define USER_FETCH_STACK_VA         (USER_DATA_WINDOW_BASE + 0x6000U)

static uint8_t snake_state_region[USER_SNAKE_STATE_PAGES * USER_PAGE_SIZE] __attribute__((aligned(USER_PAGE_SIZE)));
static uint8_t snake_stack_region[USER_SNAKE_STACK_PAGES * USER_PAGE_SIZE] __attribute__((aligned(USER_PAGE_SIZE)));
static uint8_t netdemo_state_region[USER_NETDEMO_STATE_PAGES * USER_PAGE_SIZE] __attribute__((aligned(USER_PAGE_SIZE)));
static uint8_t netdemo_stack_region[USER_NETDEMO_STACK_PAGES * USER_PAGE_SIZE] __attribute__((aligned(USER_PAGE_SIZE)));
static uint8_t fetch_state_region[USER_FETCH_STATE_PAGES * USER_PAGE_SIZE] __attribute__((aligned(USER_PAGE_SIZE)));
static uint8_t fetch_stack_region[USER_FETCH_STACK_PAGES * USER_PAGE_SIZE] __attribute__((aligned(USER_PAGE_SIZE)));
static int user_memory_ready = 0;

user_snake_state_t* user_snake_state_ptr = (user_snake_state_t*)snake_state_region;
user_netdemo_state_t* user_netdemo_state_ptr = (user_netdemo_state_t*)netdemo_state_region;
user_fetch_state_t* user_fetch_state_ptr = (user_fetch_state_t*)fetch_state_region;
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
    reset_user_snake_state();
    memset(&user_netdemo_state, 0, sizeof(user_netdemo_state));
    memset(&user_fetch_state, 0, sizeof(user_fetch_state));
    invalidate_snake_frame_cache();
    snake_input_pending = -1;
    snake_running = 0;
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

void run_user_tasks() {
    if (snake_running) {
        dispatch_user_task(USER_TASK_SNAKE, &snake_context);
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

void queue_user_snake_input(int input) {
    snake_input_pending = input;
}

int consume_user_snake_input() {
    int input = snake_input_pending;
    snake_input_pending = -1;
    return input;
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
    }
}
