#ifndef USERMODE_H
#define USERMODE_H

#include <stdint.h>
#include "fs.h"
#include "gdt.h"
#include "net.h"
#include "syscall.h"

#define USER_APP_STATUS_OK 0
#define USER_APP_STATUS_RUNNING 1

typedef struct {
    int px[100];
    int py[100];
    int len;
    int apple_x;
    int apple_y;
    int dead;
    int score;
    int best;
    int dir;
    int last_tick;
} user_snake_state_t;

typedef struct {
    int status;
    net_http_result_t result;
    char host[96];
    char path[160];
    char response[2048];
} user_netdemo_state_t;

typedef struct {
    int status;
    uint32_t body_offset;
    uint32_t saved_len;
    net_http_result_t result;
    char host[96];
    char path[160];
    char output_path[128];
    char response[4096];
} user_fetch_state_t;

typedef struct {
    int status;
    int exit_code;
    net_http_result_t http_result;
    net_ping_result_t ping_result;
    rtc_local_time_t local_time;
    disk_fs_node_t dir_entries[MAX_FILES];
    char command[128];
    char scratch[4096];
    char aux[4096];
} user_shell_state_t;

#define USER_GUI_EVENT_QUEUE_CAP 64

typedef enum {
    USER_NARCPAD_EVT_NONE = 0,
    USER_NARCPAD_EVT_CHAR,
    USER_NARCPAD_EVT_BACKSPACE,
    USER_NARCPAD_EVT_NEWLINE,
    USER_NARCPAD_EVT_SAVE,
    USER_NARCPAD_EVT_OPEN_NEW,
    USER_NARCPAD_EVT_OPEN_PATH
} user_narcpad_event_t;

typedef enum {
    USER_SETTINGS_EVT_NONE = 0,
    USER_SETTINGS_EVT_ADJUST_OFFSET,
    USER_SETTINGS_EVT_SET_OFFSET,
    USER_SETTINGS_EVT_OPEN_CONFIG
} user_settings_event_t;

typedef enum {
    USER_EXPLORER_MODAL_NONE = 0,
    USER_EXPLORER_MODAL_RENAME,
    USER_EXPLORER_MODAL_DELETE
} user_explorer_modal_t;

typedef enum {
    USER_EXPLORER_EVT_NONE = 0,
    USER_EXPLORER_EVT_OPEN_DIR,
    USER_EXPLORER_EVT_SELECT_IDX,
    USER_EXPLORER_EVT_OPEN_SELECTED,
    USER_EXPLORER_EVT_CREATE_FILE,
    USER_EXPLORER_EVT_CREATE_DIR,
    USER_EXPLORER_EVT_BEGIN_RENAME,
    USER_EXPLORER_EVT_BEGIN_DELETE,
    USER_EXPLORER_EVT_MODAL_CHAR,
    USER_EXPLORER_EVT_MODAL_BACKSPACE,
    USER_EXPLORER_EVT_MODAL_SUBMIT,
    USER_EXPLORER_EVT_MODAL_CANCEL,
    USER_EXPLORER_EVT_MOVE_SELECTED_TO,
    USER_EXPLORER_EVT_GO_BACK,
    USER_EXPLORER_EVT_GO_UP,
    USER_EXPLORER_EVT_REFRESH
} user_explorer_event_t;

typedef struct {
    int status;
    int dirty;
    int event_head;
    int event_tail;
    uint16_t event_type[USER_GUI_EVENT_QUEUE_CAP];
    int32_t event_arg[USER_GUI_EVENT_QUEUE_CAP];
    char title[32];
    char path[256];
    char request_path[256];
    char content[1024];
} user_narcpad_state_t;

typedef struct {
    int status;
    int dirty;
    int event_head;
    int event_tail;
    uint16_t event_type[USER_GUI_EVENT_QUEUE_CAP];
    int32_t event_arg[USER_GUI_EVENT_QUEUE_CAP];
} user_settings_state_t;

typedef struct {
    int status;
    int dirty;
    int current_dir;
    int prev_dir;
    int selected_idx;
    int modal_mode;
    int modal_input_len;
    int event_head;
    int event_tail;
    uint16_t event_type[USER_GUI_EVENT_QUEUE_CAP];
    int32_t event_arg[USER_GUI_EVENT_QUEUE_CAP];
    char modal_input[32];
} user_explorer_state_t;

int init_usermode();
void launch_user_snake();
void launch_user_narcpad();
void launch_user_settings();
void launch_user_explorer(int initial_dir);
void run_user_tasks();
int run_user_netdemo(const char* target);
int run_user_fetch(const char* args);
int run_user_shell_command(const char* command);
void stop_user_snake();
int user_snake_running();
int user_narcpad_running();
int user_settings_running();
int user_explorer_running();
void queue_user_snake_input(int input);
int consume_user_snake_input();
void request_user_narcpad_new();
void request_user_narcpad_open(const char* path);
void queue_user_narcpad_event(int type, int value);
void queue_user_settings_event(int type, int value);
void queue_user_explorer_event(int type, int value);
void user_yield_handler(trap_frame_t* frame);

extern user_snake_state_t* user_snake_state_ptr;
extern user_netdemo_state_t* user_netdemo_state_ptr;
extern user_fetch_state_t* user_fetch_state_ptr;
extern user_shell_state_t* user_shell_state_ptr;
extern user_narcpad_state_t* user_narcpad_state_ptr;
extern user_settings_state_t* user_settings_state_ptr;
extern user_explorer_state_t* user_explorer_state_ptr;
#define user_snake_state    (*user_snake_state_ptr)
#define user_netdemo_state  (*user_netdemo_state_ptr)
#define user_fetch_state    (*user_fetch_state_ptr)
#define user_shell_state    (*user_shell_state_ptr)
#define user_narcpad_state  (*user_narcpad_state_ptr)
#define user_settings_state (*user_settings_state_ptr)
#define user_explorer_state (*user_explorer_state_ptr)
extern uint32_t user_kernel_resume_esp;
extern uint32_t user_kernel_ebx;
extern uint32_t user_kernel_esi;
extern uint32_t user_kernel_edi;
extern uint32_t user_kernel_ebp;
extern void run_user_task(trap_frame_t* frame);

#endif
