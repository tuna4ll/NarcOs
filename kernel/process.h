#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>

typedef enum {
    PROC_UNUSED = 0,
    PROC_RUNNABLE,
    PROC_RUNNING,
    PROC_ZOMBIE
} process_state_t;

typedef void (*process_entry_t)(void*);

typedef struct process {
    int pid;
    process_state_t state;
    uint32_t esp;
    uint32_t cr3;
    void* stack_base;
    uint32_t stack_pages;
    uint32_t kernel_stack_top;
    process_entry_t entry;
    void* arg;
    char name[32];
    uint32_t ticks;
    uint32_t yields;
} process_t;

void process_init();
int process_create_kernel(const char* name, process_entry_t entry, void* arg);
void scheduler_start();
void process_yield();
void process_poll();
void process_exit_current();
void process_on_timer_tick();
process_t* process_current();
int process_current_pid();

extern volatile int scheduler_pending;
extern void process_switch(uint32_t* old_esp, uint32_t new_esp);

#endif
