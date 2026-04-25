#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include "arch.h"
#include "exec.h"
#include "process_api.h"

#define PROCESS_FLAG_USER_EXIT_PENDING 0x00000001U
#define PROCESS_MAX_ARGS 8
#define PROCESS_MAX_ARG_LEN 64
#define PROCESS_MAX_FDS 16

typedef struct fd_handle fd_handle_t;

typedef enum {
    PROCESS_USER_REQ_NONE = 0,
    PROCESS_USER_REQ_EXEC,
    PROCESS_USER_REQ_WAITPID,
    PROCESS_USER_REQ_SLEEP,
    PROCESS_USER_REQ_READ,
    PROCESS_USER_REQ_WRITE
} process_user_request_t;

typedef enum {
    PROC_UNUSED = 0,
    PROC_RUNNABLE,
    PROC_RUNNING,
    PROC_ZOMBIE
} process_state_t;

typedef enum {
    PROCESS_KIND_KERNEL = 0,
    PROCESS_KIND_USER
} process_kind_t;

typedef void (*process_entry_t)(void*);

typedef struct process {
    int pid;
    int parent_pid;
    process_state_t state;
    process_kind_t kind;
    int exit_code;
    uint32_t flags;
    uint8_t killed;
    uint8_t waitable;
    uint16_t reserved0;
    uint32_t live_children;
    uint32_t zombie_children;
    uintptr_t user_entry;
    uintptr_t user_stack_top;
    int user_argc;
    char user_args[PROCESS_MAX_ARGS][PROCESS_MAX_ARG_LEN];
    int cwd_node;
    arch_process_state_t arch;
    exec_address_space_t user_space;
    fd_handle_t* fd_table[PROCESS_MAX_FDS];
    process_entry_t entry;
    void* arg;
    char name[32];
    char image_path[128];
    char pending_exec_path[128];
    process_user_request_t pending_request;
    int pending_wait_pid;
    uint32_t pending_wait_flags;
    uintptr_t pending_wait_status_ptr;
    uint32_t pending_sleep_until;
    int pending_io_fd;
    uintptr_t pending_io_user_ptr;
    uint32_t pending_io_len;
    uint32_t ticks;
    uint32_t yields;
} process_t;

void process_init();
int process_create_kernel(const char* name, process_entry_t entry, void* arg);
int process_create_user(const char* path, const char* const* argv, int argc, uint32_t flags);
int process_request_exec_current(const char* path, const char* const* argv, int argc);
int process_request_wait_current(int pid, uintptr_t status_user_ptr, uint32_t flags);
int process_request_sleep_current(uint32_t ticks);
int process_request_read_current(int fd, uintptr_t buffer_user_ptr, uint32_t len);
int process_request_write_current(int fd, uintptr_t buffer_user_ptr, uint32_t len);
int process_kill_pid(int pid);
int process_waitpid_sync_current(int pid, uint32_t flags, int* out_status);
int process_snapshot(process_snapshot_entry_t* out_entries, int max_entries);
void process_debug_dump(const char* tag);
void scheduler_start();
void process_yield();
void process_poll();
void process_exit_current(int exit_code);
void process_on_timer_tick();
process_t* process_current();
int process_current_pid();
int process_current_ppid();

extern volatile int scheduler_pending;

#endif
