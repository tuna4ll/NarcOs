#include <stdint.h>
#include "string.h"
#include "usermode.h"
#include "user_abi.h"

#define USER_CODE __attribute__((section(".user_code")))

#include "user_string.h"

#define strlen user_strlen
#define strncpy user_strncpy
#define memset user_memset

static USER_CODE int narcpad_dequeue_event(user_narcpad_state_t* state, int* out_type, int* out_value) {
    int head;

    if (!state || !out_type || !out_value) return 0;
    if (state->event_head == state->event_tail) return 0;
    head = state->event_head;
    *out_type = state->event_type[head];
    *out_value = state->event_arg[head];
    state->event_head = (head + 1) % USER_GUI_EVENT_QUEUE_CAP;
    return 1;
}

static USER_CODE void narcpad_copy_title_from_path(user_narcpad_state_t* state, const char* path) {
    const char* name = path;

    if (!state || !path || path[0] == '\0') return;
    while (*path) {
        if (*path == '/' && path[1] != '\0') name = path + 1;
        path++;
    }
    strncpy(state->title, name, sizeof(state->title) - 1U);
    state->title[sizeof(state->title) - 1U] = '\0';
}

static USER_CODE void narcpad_open_new(user_narcpad_state_t* state) {
    if (!state) return;
    state->path[0] = '\0';
    state->request_path[0] = '\0';
    state->content[0] = '\0';
    strncpy(state->title, "untitled.txt", sizeof(state->title) - 1U);
    state->title[sizeof(state->title) - 1U] = '\0';
    state->dirty = 1;
}

static USER_CODE void narcpad_open_path(user_narcpad_state_t* state) {
    if (!state || state->request_path[0] == '\0') return;
    strncpy(state->path, state->request_path, sizeof(state->path) - 1U);
    state->path[sizeof(state->path) - 1U] = '\0';
    if (user_fs_read(state->path, state->content, sizeof(state->content)) != 0) {
        state->content[0] = '\0';
    }
    narcpad_copy_title_from_path(state, state->path);
    state->request_path[0] = '\0';
    state->dirty = 1;
}

static USER_CODE void narcpad_save(user_narcpad_state_t* state) {
    const char* target;

    if (!state) return;
    target = state->path[0] != '\0' ? state->path : state->title;
    if (!target || target[0] == '\0') return;
    if (user_fs_write(target, state->content) == 0 && state->path[0] == '\0') {
        strncpy(state->path, target, sizeof(state->path) - 1U);
        state->path[sizeof(state->path) - 1U] = '\0';
    }
    state->dirty = 1;
}

static USER_CODE void narcpad_backspace(user_narcpad_state_t* state) {
    uint32_t len;

    if (!state) return;
    len = (uint32_t)strlen(state->content);
    if (len == 0U) return;
    state->content[len - 1U] = '\0';
    state->dirty = 1;
}

static USER_CODE void narcpad_append_char(user_narcpad_state_t* state, char c) {
    uint32_t len;

    if (!state || c == '\0') return;
    len = (uint32_t)strlen(state->content);
    if (len + 1U >= sizeof(state->content)) return;
    state->content[len] = c;
    state->content[len + 1U] = '\0';
    state->dirty = 1;
}

static USER_CODE void narcpad_append_newline(user_narcpad_state_t* state) {
    narcpad_append_char(state, '\n');
}

void USER_CODE user_narcpad_entry_c(user_narcpad_state_t* state) {
    if (!state) return;
    if (state->title[0] == '\0') narcpad_open_new(state);

    for (;;) {
        int event_type;
        int event_value;

        while (narcpad_dequeue_event(state, &event_type, &event_value)) {
            switch (event_type) {
                case USER_NARCPAD_EVT_CHAR:
                    narcpad_append_char(state, (char)event_value);
                    break;
                case USER_NARCPAD_EVT_BACKSPACE:
                    narcpad_backspace(state);
                    break;
                case USER_NARCPAD_EVT_NEWLINE:
                    narcpad_append_newline(state);
                    break;
                case USER_NARCPAD_EVT_SAVE:
                    narcpad_save(state);
                    break;
                case USER_NARCPAD_EVT_OPEN_NEW:
                    narcpad_open_new(state);
                    break;
                case USER_NARCPAD_EVT_OPEN_PATH:
                    narcpad_open_path(state);
                    break;
                default:
                    break;
            }
        }
        user_yield();
    }
}
