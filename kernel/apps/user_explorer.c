#include <stdint.h>
#include "string.h"
#include "fs.h"
#include "usermode.h"
#include "user_abi.h"

#define USER_CODE __attribute__((section(".user_code")))

#include "user_string.h"

#define strlen user_strlen
#define strncpy user_strncpy
#define memset user_memset

static USER_CODE int explorer_dequeue_event(user_explorer_state_t* state, int* out_type, int* out_value) {
    int head;

    if (!state || !out_type || !out_value) return 0;
    if (state->event_head == state->event_tail) return 0;
    head = state->event_head;
    *out_type = state->event_type[head];
    *out_value = state->event_arg[head];
    state->event_head = (head + 1) % USER_GUI_EVENT_QUEUE_CAP;
    return 1;
}

static USER_CODE int explorer_append_text(char* dst, int dst_len, const char* src) {
    int off = 0;

    if (!dst || dst_len <= 0 || !src) return -1;
    while (dst[off] != '\0') off++;
    while (*src) {
        if (off + 1 >= dst_len) return -1;
        dst[off++] = *src++;
    }
    dst[off] = '\0';
    return 0;
}

static USER_CODE int explorer_append_uint(char* dst, int dst_len, uint32_t value) {
    char digits[12];
    int count = 0;

    if (value == 0U) return explorer_append_text(dst, dst_len, "0");
    while (value != 0U && count < (int)sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (count > 0) {
        char c[2];
        c[0] = digits[--count];
        c[1] = '\0';
        if (explorer_append_text(dst, dst_len, c) != 0) return -1;
    }
    return 0;
}

static USER_CODE int explorer_selected_valid(user_explorer_state_t* state, disk_fs_node_t* out_node) {
    if (!state || state->selected_idx < 0) return 0;
    return user_fs_get_node_info(state->selected_idx, out_node) == 0;
}

static USER_CODE void explorer_cancel_modal(user_explorer_state_t* state) {
    if (!state) return;
    state->modal_mode = USER_EXPLORER_MODAL_NONE;
    state->modal_input_len = 0;
    state->modal_input[0] = '\0';
    state->dirty = 1;
}

static USER_CODE void explorer_open_dir(user_explorer_state_t* state, int dir_idx) {
    disk_fs_node_t node;

    if (!state || dir_idx < -1) return;
    if (dir_idx >= 0) {
        if (user_fs_get_node_info(dir_idx, &node) != 0) return;
        if (node.flags != FS_NODE_DIR) return;
    }
    if (state->current_dir != dir_idx) state->prev_dir = state->current_dir;
    state->current_dir = dir_idx;
    state->selected_idx = -1;
    state->list_scroll = 0;
    explorer_cancel_modal(state);
    state->dirty = 1;
}

static USER_CODE void explorer_build_path_for_idx(int idx, char* out, int out_len) {
    if (!out || out_len <= 0) return;
    out[0] = '\0';
    (void)user_fs_get_path(idx, out, (uint32_t)out_len);
}

static USER_CODE void explorer_open_selected(user_explorer_state_t* state) {
    disk_fs_node_t node;
    char path[256];

    if (!explorer_selected_valid(state, &node)) return;
    if (node.flags == FS_NODE_DIR) {
        explorer_open_dir(state, state->selected_idx);
        return;
    }
    explorer_build_path_for_idx(state->selected_idx, path, sizeof(path));
    if (path[0] != '\0') (void)user_gui_open_narcpad_file(path);
}

static USER_CODE void explorer_create_in_current_dir(user_explorer_state_t* state, int is_dir) {
    char path[320];
    int n;

    if (!state) return;
    for (n = 1; n < 100; n++) {
        path[0] = '\0';
        explorer_build_path_for_idx(state->current_dir, path, sizeof(path));
        if (!(path[0] == '/' && path[1] == '\0')) {
            if (explorer_append_text(path, sizeof(path), "/") != 0) return;
        }
        if (is_dir) {
            if (explorer_append_text(path, sizeof(path), "NewFolder") != 0) return;
        } else {
            if (explorer_append_text(path, sizeof(path), "NewFile") != 0) return;
        }
        if (n > 1) {
            if (explorer_append_uint(path, sizeof(path), (uint32_t)n) != 0) return;
        }
        if (!is_dir && explorer_append_text(path, sizeof(path), ".txt") != 0) return;
        if (user_fs_find_node(path) == -1) {
            if (is_dir) {
                if (user_fs_mkdir(path) == 0) state->dirty = 1;
            } else {
                if (user_fs_touch(path) == 0) state->dirty = 1;
            }
            return;
        }
    }
}

static USER_CODE void explorer_begin_rename(user_explorer_state_t* state) {
    disk_fs_node_t node;
    int i = 0;

    if (!explorer_selected_valid(state, &node)) return;
    while (node.name[i] != '\0' && i < (int)sizeof(state->modal_input) - 1) {
        state->modal_input[i] = node.name[i];
        i++;
    }
    state->modal_input[i] = '\0';
    state->modal_input_len = i;
    state->modal_mode = USER_EXPLORER_MODAL_RENAME;
    state->dirty = 1;
}

static USER_CODE void explorer_begin_delete(user_explorer_state_t* state) {
    disk_fs_node_t node;

    if (!explorer_selected_valid(state, &node)) return;
    state->modal_mode = USER_EXPLORER_MODAL_DELETE;
    state->dirty = 1;
}

static USER_CODE void explorer_modal_append_char(user_explorer_state_t* state, char c) {
    if (!state || state->modal_mode != USER_EXPLORER_MODAL_RENAME) return;
    if (c == '\0' || c == '/' || state->modal_input_len >= (int)sizeof(state->modal_input) - 1) return;
    state->modal_input[state->modal_input_len++] = c;
    state->modal_input[state->modal_input_len] = '\0';
    state->dirty = 1;
}

static USER_CODE void explorer_modal_backspace(user_explorer_state_t* state) {
    if (!state || state->modal_mode != USER_EXPLORER_MODAL_RENAME || state->modal_input_len <= 0) return;
    state->modal_input_len--;
    state->modal_input[state->modal_input_len] = '\0';
    state->dirty = 1;
}

static USER_CODE void explorer_delete_selected(user_explorer_state_t* state) {
    char path[256];
    disk_fs_node_t node;

    if (!explorer_selected_valid(state, &node)) return;
    explorer_build_path_for_idx(state->selected_idx, path, sizeof(path));
    if (path[0] == '\0') return;
    if (user_fs_delete(path) == 0) {
        state->selected_idx = -1;
        state->dirty = 1;
    }
}

static USER_CODE void explorer_submit_modal(user_explorer_state_t* state) {
    char path[256];
    disk_fs_node_t node;

    if (!state) return;
    if (state->modal_mode == USER_EXPLORER_MODAL_RENAME) {
        if (state->modal_input[0] != '\0' && explorer_selected_valid(state, &node)) {
            explorer_build_path_for_idx(state->selected_idx, path, sizeof(path));
            if (path[0] != '\0' && user_fs_rename(path, state->modal_input) == 0) {
                state->dirty = 1;
            }
        }
        explorer_cancel_modal(state);
    } else if (state->modal_mode == USER_EXPLORER_MODAL_DELETE) {
        explorer_delete_selected(state);
        explorer_cancel_modal(state);
    }
}

static USER_CODE void explorer_move_selected_to(user_explorer_state_t* state, int target_dir) {
    char source_path[256];
    char target_path[256];
    disk_fs_node_t source_node;
    disk_fs_node_t target_node;

    if (!explorer_selected_valid(state, &source_node)) return;
    if (target_dir >= 0) {
        if (user_fs_get_node_info(target_dir, &target_node) != 0 || target_node.flags != FS_NODE_DIR) return;
    }
    explorer_build_path_for_idx(state->selected_idx, source_path, sizeof(source_path));
    explorer_build_path_for_idx(target_dir, target_path, sizeof(target_path));
    if (source_path[0] == '\0' || target_path[0] == '\0') return;
    if (user_fs_move(source_path, target_path) == 0) {
        state->selected_idx = -1;
        state->dirty = 1;
    }
}

static USER_CODE void explorer_go_up(user_explorer_state_t* state) {
    disk_fs_node_t node;

    if (!state || state->current_dir < 0) return;
    if (user_fs_get_node_info(state->current_dir, &node) != 0) return;
    explorer_open_dir(state, node.parent_index);
}

void USER_CODE user_explorer_entry_c(user_explorer_state_t* state) {
    if (!state) return;

    for (;;) {
        int event_type;
        int event_value;

        while (explorer_dequeue_event(state, &event_type, &event_value)) {
            switch (event_type) {
                case USER_EXPLORER_EVT_OPEN_DIR:
                    explorer_open_dir(state, event_value);
                    break;
                case USER_EXPLORER_EVT_SELECT_IDX:
                    state->selected_idx = event_value;
                    state->dirty = 1;
                    break;
                case USER_EXPLORER_EVT_OPEN_SELECTED:
                    explorer_open_selected(state);
                    break;
                case USER_EXPLORER_EVT_CREATE_FILE:
                    explorer_create_in_current_dir(state, 0);
                    break;
                case USER_EXPLORER_EVT_CREATE_DIR:
                    explorer_create_in_current_dir(state, 1);
                    break;
                case USER_EXPLORER_EVT_BEGIN_RENAME:
                    explorer_begin_rename(state);
                    break;
                case USER_EXPLORER_EVT_BEGIN_DELETE:
                    explorer_begin_delete(state);
                    break;
                case USER_EXPLORER_EVT_MODAL_CHAR:
                    explorer_modal_append_char(state, (char)event_value);
                    break;
                case USER_EXPLORER_EVT_MODAL_BACKSPACE:
                    explorer_modal_backspace(state);
                    break;
                case USER_EXPLORER_EVT_MODAL_SUBMIT:
                    explorer_submit_modal(state);
                    break;
                case USER_EXPLORER_EVT_MODAL_CANCEL:
                    explorer_cancel_modal(state);
                    break;
                case USER_EXPLORER_EVT_MOVE_SELECTED_TO:
                    explorer_move_selected_to(state, event_value);
                    break;
                case USER_EXPLORER_EVT_GO_BACK:
                    explorer_open_dir(state, state->prev_dir);
                    break;
                case USER_EXPLORER_EVT_GO_UP:
                    explorer_go_up(state);
                    break;
                case USER_EXPLORER_EVT_REFRESH:
                    state->dirty = 1;
                    break;
                default:
                    break;
            }
        }
        user_yield();
    }
}
