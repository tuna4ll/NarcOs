#include "usermode.h"

#include "exec.h"
#include "paging.h"
#include "process.h"
#include "serial.h"
#include "string.h"

static process_t* active_external_process = (process_t*)0;

static user_snake_state_t snake_state;
static user_netdemo_state_t netdemo_state;
static user_fetch_state_t fetch_state;
static user_shell_state_t shell_state;
static user_narcpad_state_t narcpad_state;
static user_settings_state_t settings_state;
static user_explorer_state_t explorer_state;

user_snake_state_t* user_snake_state_ptr = &snake_state;
user_netdemo_state_t* user_netdemo_state_ptr = &netdemo_state;
user_fetch_state_t* user_fetch_state_ptr = &fetch_state;
user_shell_state_t* user_shell_state_ptr = &shell_state;
user_narcpad_state_t* user_narcpad_state_ptr = &narcpad_state;
user_settings_state_t* user_settings_state_ptr = &settings_state;
user_explorer_state_t* user_explorer_state_ptr = &explorer_state;

uintptr_t user_kernel_resume_esp = 0;
uintptr_t user_kernel_ebx = 0;
uintptr_t user_kernel_esi = 0;
uintptr_t user_kernel_edi = 0;
uintptr_t user_kernel_ebp = 0;
uintptr_t user_kernel_return_mode = USER_KERNEL_RETURN_NONE;
arch_trap_frame_t* user_current_task_frame_ptr = (arch_trap_frame_t*)0;

static void serial_write_hex_uintptr(uintptr_t value) {
#if UINTPTR_MAX > 0xFFFFFFFFU
    serial_write_hex64((uint64_t)value);
#else
    serial_write_hex32((uint32_t)value);
#endif
}

static int build_process_initial_stack(process_t* proc, const exec_image_t* image) {
    uintptr_t arg_ptrs[PROCESS_MAX_ARGS];
    uintptr_t sp;
    uintptr_t argv_base;

    if (!proc || !image) return -1;
    if (proc->user_argc < 0 || proc->user_argc > PROCESS_MAX_ARGS) return -1;

    sp = image->stack_top;
    for (int i = proc->user_argc - 1; i >= 0; i--) {
        uint32_t len = (uint32_t)strlen(proc->user_args[i]) + 1U;

        if (len > PROCESS_MAX_ARG_LEN) return -1;
        if (sp < (uintptr_t)image->stack_base + len) return -1;
        sp -= len;
        memcpy((void*)sp, proc->user_args[i], len);
        arg_ptrs[i] = sp;
    }

    if (image->image_class != EXEC_IMAGE_CLASS_ELF64) return -1;

    sp &= ~(uintptr_t)0xFUL;
    if (sp < (uintptr_t)image->stack_base + sizeof(uint64_t)) return -1;
    sp -= sizeof(uint64_t);
    *(uint64_t*)sp = 0ULL;
    for (int i = proc->user_argc - 1; i >= 0; i--) {
        if (sp < (uintptr_t)image->stack_base + sizeof(uint64_t)) return -1;
        sp -= sizeof(uint64_t);
        *(uint64_t*)sp = (uint64_t)arg_ptrs[i];
    }

    argv_base = sp;
    if (sp < (uintptr_t)image->stack_base + sizeof(uint64_t)) return -1;
    sp -= sizeof(uint64_t);
    *(uint64_t*)sp = (uint64_t)proc->user_argc;
    arch_user_frame_set_exec_start(&proc->arch.user_frame, (uintptr_t)proc->user_argc, argv_base, sp);
    return 0;
}

static int validate_process_context(process_t* proc) {
    exec_image_t image;
    uintptr_t ip;
    uintptr_t sp;

    if (!proc) return 0;
    if (exec_query_image(&proc->user_space, &image) != EXEC_OK) return 0;

    ip = arch_frame_user_ip(&proc->arch.user_frame);
    sp = arch_frame_user_sp(&proc->arch.user_frame);
    if (ip < image.image_base || ip >= image.image_limit) return 0;
    if (sp < image.stack_base || sp > image.stack_top) return 0;
    return 1;
}

int init_usermode(void) {
    memset(&snake_state, 0, sizeof(snake_state));
    memset(&netdemo_state, 0, sizeof(netdemo_state));
    memset(&fetch_state, 0, sizeof(fetch_state));
    memset(&shell_state, 0, sizeof(shell_state));
    memset(&narcpad_state, 0, sizeof(narcpad_state));
    memset(&settings_state, 0, sizeof(settings_state));
    memset(&explorer_state, 0, sizeof(explorer_state));
    active_external_process = (process_t*)0;
    user_kernel_resume_esp = 0;
    user_kernel_ebx = 0;
    user_kernel_esi = 0;
    user_kernel_edi = 0;
    user_kernel_ebp = 0;
    user_kernel_return_mode = USER_KERNEL_RETURN_NONE;
    user_current_task_frame_ptr = (arch_trap_frame_t*)0;
    return 0;
}

int usermode_prepare_process_context(process_t* proc) {
    exec_image_t image;

    if (!proc) return -1;
    if (exec_activate_address_space(&proc->user_space) != EXEC_OK) return -1;
    if (exec_query_image(&proc->user_space, &image) != EXEC_OK) return -1;
    if (image.image_class != EXEC_IMAGE_CLASS_ELF64) return -1;

    arch_user_frame_init(&proc->arch.user_frame, image.entry_point, image.stack_top);
    arch_user_frame_set_exec_class(&proc->arch.user_frame, image.image_class);
    proc->user_entry = image.entry_point;
    proc->user_stack_top = image.stack_top;
    return build_process_initial_stack(proc, &image);
}

int usermode_run_external_process(process_t* proc) {
    if (!proc) return -1;
    if (exec_activate_address_space(&proc->user_space) != EXEC_OK) return -1;
    if (!validate_process_context(proc)) return -1;

    active_external_process = proc;
    user_current_task_frame_ptr = &proc->arch.user_frame;
    user_kernel_return_mode = USER_KERNEL_RETURN_NONE;
    arch_set_kernel_stack(proc->arch.user_trap_stack_top);
    arch_user_frame_sanitize(&proc->arch.user_frame);
    arch_enter_user(&proc->arch.user_frame);

    user_current_task_frame_ptr = (arch_trap_frame_t*)0;
    active_external_process = (process_t*)0;
    return 0;
}

int usermode_schedule_current_process_exit(int exit_code) {
    (void)exit_code;
    return -1;
}

uintptr_t usermode_active_trap_stack_top(void) {
    process_t* current = process_current();

    if (current && current->kind == PROCESS_KIND_USER && current->arch.user_trap_stack_top != 0U) {
        return current->arch.user_trap_stack_top;
    }
    if (active_external_process && active_external_process->arch.user_trap_stack_top != 0U) {
        return active_external_process->arch.user_trap_stack_top;
    }
    return (uintptr_t)KERNEL_BOOT_STACK_TOP;
}

void stop_user_snake(void) {}
int user_snake_running(void) { return 0; }
int user_narcpad_running(void) { return 0; }
int user_settings_running(void) { return 0; }
int user_explorer_running(void) { return 0; }
void launch_user_snake(void) {}
void launch_user_narcpad(void) {}
void launch_user_settings(void) {}
void launch_user_explorer(int initial_dir) { (void)initial_dir; }
void run_user_tasks(void) {}
void stop_all_background_user_tasks(void) {}
int run_user_netdemo(const char* target) { (void)target; return -1; }
int run_user_https_command(const char* target) { (void)target; return -1; }
int run_user_fetch(const char* args) { (void)args; return -1; }
int run_user_shell_command(const char* command) { (void)command; return -1; }
void queue_user_snake_input(int input) { (void)input; }
int consume_user_snake_input(void) { return -1; }
void request_user_narcpad_new(void) {}
void request_user_narcpad_open(const char* path) { (void)path; }
void queue_user_narcpad_event(int type, int value) { (void)type; (void)value; }
void queue_user_settings_event(int type, int value) { (void)type; (void)value; }
void queue_user_explorer_event(int type, int value) { (void)type; (void)value; }

void user_yield_handler(arch_trap_frame_t* frame) {
    process_t* current = process_current();

    if (!frame || !current || current->kind != PROCESS_KIND_USER) return;
    current->arch.user_frame = *frame;
}

void usermode_debug_dump(const char* tag) {
    serial_write("[x64-user] ");
    serial_write(tag ? tag : "state");
    serial_write(" active_pid=");
    serial_write_hex32((uint32_t)(active_external_process ? active_external_process->pid : 0));
    if (active_external_process) {
        serial_write(" rip=");
        serial_write_hex_uintptr(arch_frame_user_ip(&active_external_process->arch.user_frame));
        serial_write(" rsp=");
        serial_write_hex_uintptr(arch_frame_user_sp(&active_external_process->arch.user_frame));
    }
    serial_write_char('\n');
}

int usermode_exit_current_task(int exit_code) {
    (void)exit_code;
    return -1;
}
