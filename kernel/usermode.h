#ifndef USERMODE_H
#define USERMODE_H

#include <stdint.h>
#include "gdt.h"

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

void init_usermode();
void launch_user_snake();
void run_user_tasks();
void stop_user_snake();
int user_snake_running();
void queue_user_snake_input(int input);
int consume_user_snake_input();
void user_yield_handler(trap_frame_t* frame);

extern user_snake_state_t user_snake_state;
extern uint32_t user_kernel_resume_esp;
extern uint32_t user_kernel_ebx;
extern uint32_t user_kernel_esi;
extern uint32_t user_kernel_edi;
extern uint32_t user_kernel_ebp;
extern void run_user_task(trap_frame_t* frame);

#endif
