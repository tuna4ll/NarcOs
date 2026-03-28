#ifndef USERMODE_H
#define USERMODE_H

#include <stdint.h>
#include "gdt.h"
#include "net.h"

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

int init_usermode();
void launch_user_snake();
void run_user_tasks();
int run_user_netdemo(const char* target);
int run_user_fetch(const char* args);
void stop_user_snake();
int user_snake_running();
void queue_user_snake_input(int input);
int consume_user_snake_input();
void user_yield_handler(trap_frame_t* frame);

extern user_snake_state_t* user_snake_state_ptr;
extern user_netdemo_state_t* user_netdemo_state_ptr;
extern user_fetch_state_t* user_fetch_state_ptr;
#define user_snake_state    (*user_snake_state_ptr)
#define user_netdemo_state  (*user_netdemo_state_ptr)
#define user_fetch_state    (*user_fetch_state_ptr)
extern uint32_t user_kernel_resume_esp;
extern uint32_t user_kernel_ebx;
extern uint32_t user_kernel_esi;
extern uint32_t user_kernel_edi;
extern uint32_t user_kernel_ebp;
extern void run_user_task(trap_frame_t* frame);

#endif
