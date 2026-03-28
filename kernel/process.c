#include "process.h"
#include "paging.h"
#include "string.h"

#define MAX_PROCESSES 8
#define PROCESS_STACK_PAGES 4
#define SCHED_SLICE_TICKS 2

static process_t process_table[MAX_PROCESSES];
static int current_process_idx = -1;
static uint32_t bootstrap_esp = 0;

volatile int scheduler_pending = 0;

static void process_bootstrap();
static void idle_process(void* arg);

static int next_runnable_from(int start) {
    for (int offset = 1; offset <= MAX_PROCESSES; offset++) {
        int idx = (start + offset) % MAX_PROCESSES;
        if (process_table[idx].state == PROC_RUNNABLE) return idx;
    }
    return -1;
}

static void context_switch_to(int next_idx) {
    if (next_idx < 0 || next_idx >= MAX_PROCESSES) return;

    process_t* next = &process_table[next_idx];
    process_t* prev = process_current();
    uint32_t* old_esp_ptr = prev ? &prev->esp : &bootstrap_esp;

    if (prev && prev->state == PROC_RUNNING) prev->state = PROC_RUNNABLE;
    next->state = PROC_RUNNING;
    current_process_idx = next_idx;
    scheduler_pending = 0;

    process_switch(old_esp_ptr, next->esp);
}

void process_init() {
    memset(process_table, 0, sizeof(process_table));
    current_process_idx = -1;
    bootstrap_esp = 0;
    scheduler_pending = 0;
    process_create_kernel("idle", idle_process, 0);
}

int process_create_kernel(const char* name, process_entry_t entry, void* arg) {
    if (!entry) return -1;

    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROC_UNUSED || process_table[i].state == PROC_ZOMBIE) {
            slot = i;
            break;
        }
    }
    if (slot == -1) return -1;

    void* stack_base = alloc_physical_pages(PROCESS_STACK_PAGES);
    if (!stack_base) return -1;

    process_t* proc = &process_table[slot];
    memset(proc, 0, sizeof(*proc));
    proc->pid = slot + 1;
    proc->state = PROC_RUNNABLE;
    proc->cr3 = 0;
    proc->stack_base = stack_base;
    proc->stack_pages = PROCESS_STACK_PAGES;
    proc->entry = entry;
    proc->arg = arg;
    strncpy(proc->name, name ? name : "task", sizeof(proc->name) - 1);
    proc->name[sizeof(proc->name) - 1] = '\0';

    uint32_t* stack_top = (uint32_t*)((uint32_t)stack_base + PROCESS_STACK_PAGES * 4096U);
    *--stack_top = (uint32_t)process_bootstrap;
    *--stack_top = 0;
    *--stack_top = 0;
    *--stack_top = 0;
    *--stack_top = 0;
    proc->esp = (uint32_t)stack_top;
    return proc->pid;
}

void scheduler_start() {
    int next_idx = next_runnable_from(0);
    if (next_idx == -1) {
        for (;;) {
            asm volatile("hlt");
        }
    }
    context_switch_to(next_idx);
    for (;;) {
        asm volatile("hlt");
    }
}

void process_yield() {
    process_t* current = process_current();
    int start = current_process_idx < 0 ? 0 : current_process_idx;
    int next_idx = next_runnable_from(start);
    if (next_idx == -1 || next_idx == current_process_idx) {
        scheduler_pending = 0;
        return;
    }
    if (current) current->yields++;
    context_switch_to(next_idx);
}

void process_poll() {
    if (scheduler_pending) process_yield();
}

void process_exit_current() {
    process_t* current = process_current();
    if (!current) {
        for (;;) asm volatile("hlt");
    }
    current->state = PROC_ZOMBIE;
    process_yield();
    for (;;) asm volatile("hlt");
}

void process_on_timer_tick() {
    process_t* current = process_current();
    if (!current) return;
    current->ticks++;
    if ((current->ticks % SCHED_SLICE_TICKS) == 0) scheduler_pending = 1;
}

process_t* process_current() {
    if (current_process_idx < 0 || current_process_idx >= MAX_PROCESSES) return 0;
    if (process_table[current_process_idx].state == PROC_UNUSED) return 0;
    return &process_table[current_process_idx];
}

int process_current_pid() {
    process_t* proc = process_current();
    return proc ? proc->pid : 0;
}

static void process_bootstrap() {
    process_t* current = process_current();
    if (!current || !current->entry) process_exit_current();
    current->entry(current->arg);
    process_exit_current();
}

static void idle_process(void* arg) {
    (void)arg;
    for (;;) {
        asm volatile("hlt");
        process_poll();
    }
}
