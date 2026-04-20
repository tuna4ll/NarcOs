#include "process.h"
#include "fd.h"
#include "gdt.h"
#include "paging.h"
#include "serial.h"
#include "string.h"
#include "usermode.h"

#define MAX_PROCESSES 8
#define PROCESS_STACK_PAGES 4
#define PROCESS_USER_TRAP_STACK_PAGES 8
#define SCHED_SLICE_TICKS 2

static process_t process_table[MAX_PROCESSES];
static int current_process_idx = -1;
static uint32_t bootstrap_esp = 0;

volatile int scheduler_pending = 0;

static void process_bootstrap();
static void idle_process(void* arg);
static void user_process_entry(void* arg);
static process_t* find_process_by_pid(int pid);
static process_t* process_reserve_slot(process_kind_t kind, const char* name, int parent_pid);
static void release_recycled_process_metadata(process_t* proc);
static void process_set_name_from_path(char* dst, uint32_t dst_size, const char* path);
static void process_link_parent(process_t* proc);
static void process_log_created(const process_t* proc);
static void process_abandon_reserved_slot(process_t* proc);
static void process_clear_pending_request(process_t* proc);
static void process_clear_args(process_t* proc);
static int process_copy_args(process_t* proc, const char* const* argv, int argc);
static int process_reap_zombie_child(process_t* parent, process_t* child, int* out_status);
static int process_waitpid_query(process_t* parent, int pid, uint32_t flags, int* out_status);
static int process_exec_replace_current(process_t* proc);
static int process_service_user_request(process_t* proc);
static void process_reset_unused(process_t* proc);
static const char* process_state_name(process_state_t state);
static const char* process_kind_name(process_kind_t kind);

extern volatile uint32_t timer_ticks;

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
    set_tss_stack(next->kernel_stack_top);

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
    process_t* proc;
    process_t* parent = process_current();
    int parent_pid = 0;

    if (!entry) return -1;
    if (parent) parent_pid = parent->pid;

    proc = process_reserve_slot(PROCESS_KIND_KERNEL, name, parent_pid);
    if (!proc) return -1;
    if (fd_init_process(proc, parent) != 0) {
        process_abandon_reserved_slot(proc);
        return -1;
    }
    proc->entry = entry;
    proc->arg = arg;
    process_link_parent(proc);
    process_log_created(proc);
    return proc->pid;
}

int process_create_user(const char* path, const char* const* argv, int argc, uint32_t flags) {
    char proc_name[32];
    process_t* caller = process_current();
    process_t* proc;
    int parent_pid = 0;
    int status;

    (void)argv;
    if (!path || path[0] == '\0') return -1;
    if (process_current()) parent_pid = process_current()->pid;
    process_set_name_from_path(proc_name, sizeof(proc_name), path);
    proc = process_reserve_slot(PROCESS_KIND_USER, proc_name, parent_pid);
    if (!proc) return -1;
    if (fd_init_process(proc, caller) != 0) {
        process_abandon_reserved_slot(proc);
        return -1;
    }
    fd_close_from(proc, 3);
    if (process_copy_args(proc, argv, argc) != 0) {
        process_abandon_reserved_slot(proc);
        return -1;
    }

    strncpy(proc->image_path, path, sizeof(proc->image_path) - 1U);
    proc->image_path[sizeof(proc->image_path) - 1U] = '\0';
    proc->flags = flags & ~PROCESS_FLAG_USER_EXIT_PENDING;

    status = exec_load_elf32_file(path, &proc->user_space);
    if (status != EXEC_OK) {
        serial_write("[sched] user load failed ");
        serial_write(path);
        serial_write(" reason=");
        serial_write(exec_error_string(status));
        serial_write(" status=");
        serial_write_hex32((uint32_t)status);
        serial_write_char('\n');
        if (caller && caller != proc && caller->kind == PROCESS_KIND_USER && caller->user_space.valid) {
            (void)exec_activate_address_space(&caller->user_space);
        }
        process_abandon_reserved_slot(proc);
        return -1;
    }
    if (usermode_prepare_process_context(proc) != 0) {
        serial_write("[sched] user context init failed ");
        serial_write(path);
        serial_write_char('\n');
        if (caller && caller != proc && caller->kind == PROCESS_KIND_USER && caller->user_space.valid) {
            (void)exec_activate_address_space(&caller->user_space);
        }
        process_abandon_reserved_slot(proc);
        return -1;
    }
    if (caller && caller != proc && caller->kind == PROCESS_KIND_USER && caller->user_space.valid) {
        (void)exec_activate_address_space(&caller->user_space);
    }

    proc->entry = user_process_entry;
    proc->arg = proc;
    process_link_parent(proc);
    process_log_created(proc);
    return proc->pid;
}

int process_request_exec_current(const char* path, const char* const* argv, int argc) {
    process_t* current = process_current();

    if (!current || current->kind != PROCESS_KIND_USER || !path || path[0] == '\0') return -1;
    if (process_copy_args(current, argv, argc) != 0) return -1;
    strncpy(current->pending_exec_path, path, sizeof(current->pending_exec_path) - 1U);
    current->pending_exec_path[sizeof(current->pending_exec_path) - 1U] = '\0';
    current->pending_request = PROCESS_USER_REQ_EXEC;
    current->pending_wait_pid = 0;
    current->pending_wait_flags = 0U;
    current->pending_wait_status_ptr = 0U;
    current->pending_sleep_until = 0U;
    return 0;
}

int process_request_wait_current(int pid, uint32_t status_user_ptr, uint32_t flags) {
    process_t* current = process_current();

    if (!current || current->kind != PROCESS_KIND_USER) return -1;
    current->pending_request = PROCESS_USER_REQ_WAITPID;
    current->pending_wait_pid = pid;
    current->pending_wait_flags = flags;
    current->pending_wait_status_ptr = status_user_ptr;
    current->pending_sleep_until = 0U;
    current->pending_exec_path[0] = '\0';
    return 0;
}

int process_request_sleep_current(uint32_t ticks) {
    process_t* current = process_current();

    if (!current || current->kind != PROCESS_KIND_USER) return -1;
    current->pending_request = PROCESS_USER_REQ_SLEEP;
    current->pending_sleep_until = timer_ticks + ticks;
    current->pending_wait_pid = 0;
    current->pending_wait_flags = 0U;
    current->pending_wait_status_ptr = 0U;
    current->pending_exec_path[0] = '\0';
    return 0;
}

int process_kill_pid(int pid) {
    process_t* target = find_process_by_pid(pid);

    if (!target || target->kind != PROCESS_KIND_USER) return -1;
    if (target->state == PROC_UNUSED || target->state == PROC_ZOMBIE) return -1;
    target->killed = 1U;
    if (target->exit_code == 0) target->exit_code = -1;
    if (target == process_current()) target->flags |= PROCESS_FLAG_USER_EXIT_PENDING;
    return 0;
}

static process_t* process_reserve_slot(process_kind_t kind, const char* name, int parent_pid) {
    int slot = -1;
    uint32_t stack_top_addr = 0;
    uint32_t trap_stack_top_addr = 0;
    void* stack_base = 0;
    void* trap_stack_base = 0;
    process_t* proc;
    uint32_t* stack_top;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROC_UNUSED ||
            (process_table[i].state == PROC_ZOMBIE && process_table[i].waitable == 0U)) {
            slot = i;
            break;
        }
    }
    if (slot == -1) {
        serial_write_line("[sched] process table full");
        return 0;
    }

    if ((process_table[slot].state == PROC_ZOMBIE || process_table[slot].state == PROC_UNUSED) &&
        process_table[slot].stack_base != 0 &&
        process_table[slot].stack_pages == PROCESS_STACK_PAGES &&
        process_table[slot].kernel_stack_top != 0U) {
        stack_base = process_table[slot].stack_base;
        stack_top_addr = process_table[slot].kernel_stack_top;
    } else {
        stack_base = paging_alloc_kernel_stack(PROCESS_STACK_PAGES, &stack_top_addr);
        if (!stack_base) {
            serial_write("[sched] stack alloc failed for ");
            serial_write(name ? name : "task");
            serial_write_char('\n');
            return 0;
        }
    }

    if (process_table[slot].user_trap_stack_base != 0 &&
        process_table[slot].user_trap_stack_pages == PROCESS_USER_TRAP_STACK_PAGES &&
        process_table[slot].user_trap_stack_top != 0U) {
        trap_stack_base = process_table[slot].user_trap_stack_base;
        trap_stack_top_addr = process_table[slot].user_trap_stack_top;
    } else if (kind == PROCESS_KIND_USER) {
        trap_stack_base = paging_alloc_kernel_stack(PROCESS_USER_TRAP_STACK_PAGES, &trap_stack_top_addr);
        if (!trap_stack_base) {
            serial_write("[sched] trap stack alloc failed for ");
            serial_write(name ? name : "task");
            serial_write_char('\n');
            return 0;
        }
    }

    release_recycled_process_metadata(&process_table[slot]);
    proc = &process_table[slot];
    memset(proc, 0, sizeof(*proc));
    proc->pid = slot + 1;
    proc->parent_pid = parent_pid;
    proc->state = PROC_RUNNABLE;
    proc->kind = kind;
    proc->exit_code = 0;
    proc->waitable = parent_pid != 0 ? 1U : 0U;
    proc->cr3 = 0;
    proc->cwd_node = -1;
    proc->user_entry = 0;
    proc->user_stack_top = 0;
    process_clear_args(proc);
    process_clear_pending_request(proc);
    proc->stack_base = stack_base;
    proc->stack_pages = PROCESS_STACK_PAGES;
    proc->kernel_stack_top = stack_top_addr;
    proc->user_trap_stack_base = trap_stack_base;
    proc->user_trap_stack_pages = trap_stack_base ? PROCESS_USER_TRAP_STACK_PAGES : 0U;
    proc->user_trap_stack_top = trap_stack_top_addr;
    strncpy(proc->name, name ? name : "task", sizeof(proc->name) - 1);
    proc->name[sizeof(proc->name) - 1] = '\0';

    stack_top = (uint32_t*)stack_top_addr;
    *--stack_top = (uint32_t)process_bootstrap;
    *--stack_top = 0;
    *--stack_top = 0;
    *--stack_top = 0;
    *--stack_top = 0;
    proc->esp = (uint32_t)stack_top;
    return proc;
}

static void process_link_parent(process_t* proc) {
    process_t* parent;

    if (!proc || proc->parent_pid == 0) return;
    parent = find_process_by_pid(proc->parent_pid);
    if (parent) parent->live_children++;
}

static void process_log_created(const process_t* proc) {
    if (!proc) return;
    serial_write("[sched] created ");
    serial_write(proc->name);
    serial_write(" kind=");
    serial_write(process_kind_name(proc->kind));
    serial_write(" pid=");
    serial_write_hex32((uint32_t)proc->pid);
    serial_write(" ppid=");
    serial_write_hex32((uint32_t)proc->parent_pid);
    serial_write(" stack_base=");
    serial_write_hex32((uint32_t)proc->stack_base);
    serial_write(" stack_top=");
    serial_write_hex32(proc->kernel_stack_top);
    if (proc->kind == PROCESS_KIND_USER && proc->image_path[0] != '\0') {
        serial_write(" image=");
        serial_write(proc->image_path);
    }
    serial_write_char('\n');
}

static void process_abandon_reserved_slot(process_t* proc) {
    if (!proc) return;
    fd_cleanup_process(proc);
    exec_release_address_space(&proc->user_space);
    memset(&proc->user_frame, 0, sizeof(proc->user_frame));
    process_reset_unused(proc);
}

static void process_set_name_from_path(char* dst, uint32_t dst_size, const char* path) {
    const char* leaf = path;
    uint32_t i;

    if (!dst || dst_size == 0U) return;
    dst[0] = '\0';
    if (!path || path[0] == '\0') {
        strncpy(dst, "user", dst_size - 1U);
        dst[dst_size - 1U] = '\0';
        return;
    }
    for (i = 0; path[i] != '\0'; i++) {
        if (path[i] == '/') leaf = path + i + 1U;
    }
    if (*leaf == '\0') leaf = path;
    strncpy(dst, leaf, dst_size - 1U);
    dst[dst_size - 1U] = '\0';
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

void process_exit_current(int exit_code) {
    process_t* current = process_current();
    process_t* parent;

    if (!current) {
        for (;;) asm volatile("hlt");
    }
    if (current->kind == PROCESS_KIND_USER) {
        current->flags &= ~PROCESS_FLAG_USER_EXIT_PENDING;
        process_clear_pending_request(current);
        exec_release_address_space(&current->user_space);
    }
    fd_cleanup_process(current);
    current->exit_code = exit_code;
    current->state = PROC_ZOMBIE;
    parent = find_process_by_pid(current->parent_pid);
    if (parent) {
        if (parent->live_children > 0U) parent->live_children--;
        if (current->waitable) parent->zombie_children++;
    }
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

int process_current_ppid() {
    process_t* proc = process_current();
    return proc ? proc->parent_pid : 0;
}

int process_waitpid_sync_current(int pid, uint32_t flags, int* out_status) {
    process_t* current = process_current();
    int status;
    int child_status = 0;

    if (!current) return -1;
    for (;;) {
        status = process_waitpid_query(current, pid, flags, &child_status);
        if (status != -2) {
            if (out_status && status > 0) *out_status = child_status;
            return status;
        }
        if (current->killed != 0U || (current->flags & PROCESS_FLAG_USER_EXIT_PENDING) != 0U) return -1;
        process_yield();
    }
}

int process_snapshot(process_snapshot_entry_t* out_entries, int max_entries) {
    int count = 0;

    if (!out_entries || max_entries <= 0) return -1;
    for (int i = 0; i < MAX_PROCESSES && count < max_entries; i++) {
        process_t* proc = &process_table[i];

        if (proc->state == PROC_UNUSED) continue;
        memset(&out_entries[count], 0, sizeof(out_entries[count]));
        out_entries[count].pid = proc->pid;
        out_entries[count].parent_pid = proc->parent_pid;
        out_entries[count].state = (int)proc->state;
        out_entries[count].kind = (int)proc->kind;
        out_entries[count].exit_code = proc->exit_code;
        out_entries[count].flags = proc->flags;
        strncpy(out_entries[count].name, proc->name, sizeof(out_entries[count].name) - 1U);
        strncpy(out_entries[count].image_path, proc->image_path, sizeof(out_entries[count].image_path) - 1U);
        count++;
    }
    return count;
}

void process_debug_dump(const char* tag) {
    serial_write("[sched] dump");
    if (tag && tag[0] != '\0') {
        serial_write(" tag=");
        serial_write(tag);
    }
    serial_write_char('\n');

    for (int i = 0; i < MAX_PROCESSES; i++) {
        const process_t* proc = &process_table[i];

        if (proc->state == PROC_UNUSED) continue;
        serial_write("[sched] slot=");
        serial_write_hex32((uint32_t)i);
        serial_write(" pid=");
        serial_write_hex32((uint32_t)proc->pid);
        serial_write(" ppid=");
        serial_write_hex32((uint32_t)proc->parent_pid);
        serial_write(" kind=");
        serial_write(process_kind_name(proc->kind));
        serial_write(" state=");
        serial_write(process_state_name(proc->state));
        serial_write(" waitable=");
        serial_write_hex32((uint32_t)proc->waitable);
        serial_write(" killed=");
        serial_write_hex32((uint32_t)proc->killed);
        serial_write(" exit=");
        serial_write_hex32((uint32_t)proc->exit_code);
        serial_write(" live=");
        serial_write_hex32(proc->live_children);
        serial_write(" zombie=");
        serial_write_hex32(proc->zombie_children);
        serial_write(" flags=");
        serial_write_hex32(proc->flags);
        serial_write(" name=");
        serial_write(proc->name);
        if (proc->image_path[0] != '\0') {
            serial_write(" image=");
            serial_write(proc->image_path);
        }
        serial_write_char('\n');
    }
}

static void process_bootstrap() {
    process_t* current = process_current();
    if (!current || !current->entry) process_exit_current(0);
    current->entry(current->arg);
    process_exit_current(0);
}

static void user_process_entry(void* arg) {
    process_t* proc = (process_t*)arg;
    int status;

    if (!proc) proc = process_current();
    if (!proc) process_exit_current(-1);

    for (;;) {
        if (proc->killed) {
            process_exit_current(proc->exit_code != 0 ? proc->exit_code : -1);
        }
        status = process_service_user_request(proc);
        if (status < 0) {
            process_exit_current(status);
        }
        if ((proc->flags & PROCESS_FLAG_USER_EXIT_PENDING) != 0U) {
            process_exit_current(proc->exit_code);
        }
        if (status > 0) continue;
        status = usermode_run_external_process(proc);
        if (status != 0) {
            process_exit_current(status);
        }
        if ((proc->flags & PROCESS_FLAG_USER_EXIT_PENDING) != 0U) {
            process_exit_current(proc->exit_code);
        }
        process_yield();
    }
}

static void idle_process(void* arg) {
    (void)arg;
    for (;;) {
        asm volatile("hlt");
        process_poll();
    }
}

static process_t* find_process_by_pid(int pid) {
    if (pid <= 0) return 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROC_UNUSED) continue;
        if (process_table[i].pid == pid) return &process_table[i];
    }
    return 0;
}

static void release_recycled_process_metadata(process_t* proc) {
    process_t* parent;

    if (!proc || proc->state != PROC_ZOMBIE) return;
    parent = find_process_by_pid(proc->parent_pid);
    if (parent && proc->waitable && parent->zombie_children > 0U) {
        parent->zombie_children--;
    }
}

static void process_reset_unused(process_t* proc) {
    void* stack_base;
    void* trap_stack_base;
    uint32_t stack_pages;
    uint32_t kernel_stack_top;
    uint32_t trap_stack_pages;
    uint32_t trap_stack_top;

    if (!proc) return;
    stack_base = proc->stack_base;
    trap_stack_base = proc->user_trap_stack_base;
    stack_pages = proc->stack_pages;
    kernel_stack_top = proc->kernel_stack_top;
    trap_stack_pages = proc->user_trap_stack_pages;
    trap_stack_top = proc->user_trap_stack_top;
    memset(proc, 0, sizeof(*proc));
    proc->state = PROC_UNUSED;
    proc->stack_base = stack_base;
    proc->stack_pages = stack_pages;
    proc->kernel_stack_top = kernel_stack_top;
    proc->user_trap_stack_base = trap_stack_base;
    proc->user_trap_stack_pages = trap_stack_pages;
    proc->user_trap_stack_top = trap_stack_top;
}

static const char* process_state_name(process_state_t state) {
    switch (state) {
        case PROC_UNUSED: return "unused";
        case PROC_RUNNABLE: return "runnable";
        case PROC_RUNNING: return "running";
        case PROC_ZOMBIE: return "zombie";
        default: return "unknown";
    }
}

static const char* process_kind_name(process_kind_t kind) {
    return kind == PROCESS_KIND_USER ? "user" : "kernel";
}

static void process_clear_pending_request(process_t* proc) {
    if (!proc) return;
    proc->pending_request = PROCESS_USER_REQ_NONE;
    proc->pending_wait_pid = 0;
    proc->pending_wait_flags = 0U;
    proc->pending_wait_status_ptr = 0U;
    proc->pending_sleep_until = 0U;
    proc->pending_exec_path[0] = '\0';
}

static void process_clear_args(process_t* proc) {
    if (!proc) return;
    proc->user_argc = 0;
    memset(proc->user_args, 0, sizeof(proc->user_args));
}

static int process_copy_args(process_t* proc, const char* const* argv, int argc) {
    if (!proc || argc < 0 || argc > PROCESS_MAX_ARGS) return -1;
    process_clear_args(proc);
    if (argc == 0) return 0;
    if (!argv) return -1;

    for (int i = 0; i < argc; i++) {
        size_t len;

        if (!argv[i]) return -1;
        len = strlen(argv[i]);
        if (len >= PROCESS_MAX_ARG_LEN) return -1;
        strncpy(proc->user_args[i], argv[i], PROCESS_MAX_ARG_LEN - 1U);
        proc->user_args[i][PROCESS_MAX_ARG_LEN - 1U] = '\0';
    }
    proc->user_argc = argc;
    return 0;
}

static int process_reap_zombie_child(process_t* parent, process_t* child, int* out_status) {
    if (!parent || !child || child->state != PROC_ZOMBIE || child->parent_pid != parent->pid) return -1;
    if (out_status) *out_status = child->exit_code;
    if (parent->zombie_children > 0U) parent->zombie_children--;
    {
        int pid = child->pid;
        process_reset_unused(child);
        return pid;
    }
}

static int process_waitpid_query(process_t* parent, int pid, uint32_t flags, int* out_status) {
    process_t* exact_child = 0;
    int have_child = 0;

    if (!parent) return -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_t* candidate = &process_table[i];

        if (candidate->waitable == 0U || candidate->parent_pid != parent->pid) continue;
        if (pid > 0 && candidate->pid != pid) continue;
        have_child = 1;
        if (candidate->state == PROC_ZOMBIE) {
            return process_reap_zombie_child(parent, candidate, out_status);
        }
        if (pid > 0) exact_child = candidate;
    }

    if (pid > 0 && !exact_child && !have_child) return -1;
    if (!have_child) return -1;
    if ((flags & WAITPID_FLAG_NOHANG) != 0U) return 0;
    return -2;
}

static int process_exec_replace_current(process_t* proc) {
    exec_address_space_t new_space;
    int status;

    if (!proc || proc->pending_exec_path[0] == '\0') return -1;
    memset(&new_space, 0, sizeof(new_space));
    status = exec_load_elf32_file(proc->pending_exec_path, &new_space);
    if (status != EXEC_OK) {
        serial_write("[sched] exec failed pid=");
        serial_write_hex32((uint32_t)proc->pid);
        serial_write(" path=");
        serial_write(proc->pending_exec_path);
        serial_write(" reason=");
        serial_write(exec_error_string(status));
        serial_write(" status=");
        serial_write_hex32((uint32_t)status);
        serial_write_char('\n');
        if (proc->user_space.valid) (void)exec_activate_address_space(&proc->user_space);
        return -1;
    }

    exec_release_address_space(&proc->user_space);
    proc->user_space = new_space;
    if (exec_activate_address_space(&proc->user_space) != EXEC_OK) return -1;
    strncpy(proc->image_path, proc->pending_exec_path, sizeof(proc->image_path) - 1U);
    proc->image_path[sizeof(proc->image_path) - 1U] = '\0';
    process_clear_pending_request(proc);
    return usermode_prepare_process_context(proc);
}

static int process_service_user_request(process_t* proc) {
    int status;
    int child_status = 0;

    if (!proc) return -1;
    switch (proc->pending_request) {
        case PROCESS_USER_REQ_NONE:
            return 0;
        case PROCESS_USER_REQ_EXEC:
            status = process_exec_replace_current(proc);
            if (status != 0) {
                process_clear_pending_request(proc);
                proc->user_frame.eax = (uint32_t)-1;
            }
            return 0;
        case PROCESS_USER_REQ_WAITPID:
            if (exec_activate_address_space(&proc->user_space) != EXEC_OK) return -1;
            status = process_waitpid_query(proc, proc->pending_wait_pid, proc->pending_wait_flags, &child_status);
            if (status == -2) {
                process_yield();
                return 1;
            }
            if (status > 0 && proc->pending_wait_status_ptr != 0U &&
                copy_to_user((void*)proc->pending_wait_status_ptr, &child_status, sizeof(child_status)) != 0) {
                status = -1;
            }
            process_clear_pending_request(proc);
            proc->user_frame.eax = (uint32_t)status;
            return 0;
        case PROCESS_USER_REQ_SLEEP:
            if ((int32_t)(timer_ticks - proc->pending_sleep_until) < 0) {
                process_yield();
                return 1;
            }
            process_clear_pending_request(proc);
            proc->user_frame.eax = 0U;
            return 0;
        default:
            process_clear_pending_request(proc);
            return -1;
    }
}
