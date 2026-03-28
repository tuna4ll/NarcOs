#include "usermode.h"
#include "string.h"
#include "vbe.h"

extern volatile int gui_needs_redraw;
extern window_t windows[MAX_WINDOWS];
extern int active_window_idx;
extern int nwm_get_idx_by_type(window_type_t type);
extern void user_snake_entry_gate();

static trap_frame_t snake_context;
static uint8_t snake_stack[4096] __attribute__((aligned(16)));
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

user_snake_state_t user_snake_state;
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

void init_usermode() {
    memset(&snake_context, 0, sizeof(snake_context));
    reset_user_snake_state();
    invalidate_snake_frame_cache();
    snake_input_pending = -1;
    snake_running = 0;
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
    memset(&snake_context, 0, sizeof(snake_context));
    snake_context.gs = USER_DATA_SEG;
    snake_context.fs = USER_DATA_SEG;
    snake_context.es = USER_DATA_SEG;
    snake_context.ds = USER_DATA_SEG;
    snake_context.edi = (uint32_t)&user_snake_state;
    snake_context.eip = (uint32_t)user_snake_entry_gate;
    snake_context.cs = USER_CODE_SEG;
    snake_context.eflags = 0x202;
    snake_context.user_esp = (uint32_t)(snake_stack + sizeof(snake_stack) - 16);
    snake_context.user_ss = USER_DATA_SEG;

    snake_input_pending = -1;
    snake_running = 1;
    gui_needs_redraw = 1;
}

void run_user_tasks() {
    if (snake_running) run_user_task(&snake_context);
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
    int sidx = nwm_get_idx_by_type(WIN_TYPE_SNAKE);
    int should_redraw = snake_frame_changed();
    snake_context = *frame;
    remember_snake_frame();
    if (should_redraw && sidx != -1 && windows[sidx].visible && !windows[sidx].minimized) {
        gui_needs_redraw = 1;
    }
}
