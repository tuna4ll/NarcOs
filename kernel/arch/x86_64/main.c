#include <stdint.h>

#include "arch.h"
#include "cpu.h"
#include "fd.h"
#include "fs.h"
#include "memory_alloc.h"
#include "paging.h"
#include "process.h"
#include "rtc.h"
#include "serial.h"
#include "storage.h"
#include "string.h"
#include "usermode.h"

#define PHASE8_SHELL_MAX_ARGS 8
#define PHASE8_SHELL_ARG_LEN 64
#define PHASE8_CAPTURE_TMP_STDOUT 12
#define PHASE8_CAPTURE_TMP_STDERR 13
#define PHASE8_SHELL_TMP_STDIN 14
#define PHASE8_SHELL_TMP_STDOUT 15

extern volatile int cmd_ready;
extern char cmd_to_execute[128];
extern int current_dir_index;
void init_keyboard(void);
void clear_screen(void);
void vga_print(const char* str);
void vga_println(const char* str);
void vga_print_int(int num);

volatile uint32_t timer_ticks = 0;

static void phase8_halt_forever(void) {
    x64_cli();
    for (;;) {
        x64_hlt();
    }
}

static void phase8_panic(const char* message) {
    serial_write("[panic] ");
    serial_write_line(message);
    phase8_halt_forever();
}

static const char* shell_skip_spaces(const char* text) {
    while (text && *text == ' ') text++;
    return text;
}

static int shell_find_char(const char* text, char needle) {
    int idx = 0;

    if (!text) return -1;
    while (text[idx] != '\0') {
        if (text[idx] == needle) return idx;
        idx++;
    }
    return -1;
}

static int shell_has_char(const char* text, char needle) {
    return shell_find_char(text, needle) >= 0;
}

static int shell_execute_pipeline(const char* left_line, const char* right_line);

static void phase8_print_u32(char* out, size_t out_size, uint32_t value) {
    char digits[16];
    size_t count = 0;
    size_t off = 0;

    if (!out || out_size == 0U) return;
    if (value == 0U) {
        if (out_size > 1U) {
            out[0] = '0';
            out[1] = '\0';
        } else {
            out[0] = '\0';
        }
        return;
    }

    while (value != 0U && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (count > 0U && off + 1U < out_size) {
        out[off++] = digits[--count];
    }
    out[off] = '\0';
}

static int phase8_text_contains(const char* text, const char* needle) {
    size_t text_len;
    size_t needle_len;

    if (!text || !needle) return 0;
    needle_len = strlen(needle);
    if (needle_len == 0U) return 1;
    text_len = strlen(text);
    if (needle_len > text_len) return 0;
    for (size_t i = 0; i + needle_len <= text_len; i++) {
        if (memcmp(text + i, needle, needle_len) == 0) return 1;
    }
    return 0;
}

static void phase8_log_smoke_result(const char* label, int passed) {
    serial_write("[phase8] ");
    serial_write(label ? label : "smoke");
    serial_write(passed ? " OK" : " FAIL");
    serial_write_char('\n');
}

static void shell_print_prompt(void) {
    char path[256];

    fs_get_current_path(path, sizeof(path));
    vga_print("root@narc:");
    vga_print(path);
    vga_print("$ ");
}

static void shell_print_help(void) {
    vga_println("Commands:");
    vga_println("  help");
    vga_println("  clear");
    vga_println("  pwd");
    vga_println("  cd <dir>");
    vga_println("  ls");
    vga_println("  ver");
    vga_println("  hello | ps | echo | cat | kill | proc_test | pipe_test");
    vga_println("  one pipe is supported: <cmd> | <cmd>");
}

static int shell_parse_argv(const char* command, char argv_storage[PHASE8_SHELL_MAX_ARGS][PHASE8_SHELL_ARG_LEN],
                            const char* argv_ptrs[PHASE8_SHELL_MAX_ARGS], int* out_argc) {
    const char* cursor = shell_skip_spaces(command);
    int argc = 0;

    if (!cursor || !out_argc) return -1;
    while (*cursor != '\0') {
        int off = 0;

        if (argc >= PHASE8_SHELL_MAX_ARGS) return -1;
        while (*cursor != '\0' && *cursor != ' ') {
            if (off + 1 >= PHASE8_SHELL_ARG_LEN) return -1;
            argv_storage[argc][off++] = *cursor++;
        }
        argv_storage[argc][off] = '\0';
        argv_ptrs[argc] = argv_storage[argc];
        argc++;
        cursor = shell_skip_spaces(cursor);
    }

    *out_argc = argc;
    return argc > 0 ? 0 : -1;
}

static int shell_resolve_path(const char* command, char* out_path, size_t out_size) {
    int idx;
    size_t off = 0;

    if (!command || !out_path || out_size == 0U) return -1;
    if (shell_has_char(command, '/')) {
        strncpy(out_path, command, out_size - 1U);
        out_path[out_size - 1U] = '\0';
        idx = fs_find_node(out_path);
        return idx >= 0 ? 0 : -1;
    }

    strncpy(out_path, "/bin/", out_size - 1U);
    out_path[out_size - 1U] = '\0';
    off = strlen(out_path);
    while (command[0] != '\0' && off + 1U < out_size) {
        out_path[off++] = *command++;
    }
    out_path[off] = '\0';
    idx = fs_find_node(out_path);
    return idx >= 0 ? 0 : -1;
}

static int shell_wait_for_child(int pid, const char* label) {
    int status = 0;
    int wait_rc = process_waitpid_sync_current(pid, 0U, &status);

    if (wait_rc != pid) {
        vga_print("wait failed: ");
        vga_println(label ? label : "child");
        return -1;
    }
    if (status != 0) {
        vga_print(label ? label : "child");
        vga_print(" exited ");
        vga_print_int(status);
        vga_println("");
    }
    return status;
}

static int shell_spawn_and_wait(const char* path, const char* const* argv, int argc) {
    int pid = process_create_user(path, argv, argc, 0U);

    if (pid < 0) {
        vga_print("spawn failed: ");
        vga_println(path);
        return -1;
    }
    return shell_wait_for_child(pid, argv && argc > 0 ? argv[0] : path);
}

static int shell_execute_external(const char* command_line) {
    char argv_storage[PHASE8_SHELL_MAX_ARGS][PHASE8_SHELL_ARG_LEN];
    const char* argv_ptrs[PHASE8_SHELL_MAX_ARGS];
    char path[128];
    int argc = 0;

    if (shell_parse_argv(command_line, argv_storage, argv_ptrs, &argc) != 0) return -1;
    if (shell_resolve_path(argv_ptrs[0], path, sizeof(path)) != 0) {
        vga_print("command not found: ");
        vga_println(argv_ptrs[0]);
        return -1;
    }
    return shell_spawn_and_wait(path, argv_ptrs, argc);
}

static int phase8_capture_pipe_output(process_t* current, int read_fd, char* out, size_t out_size) {
    char discard[128];
    size_t used = 0;

    if (!current || read_fd < 0 || !out || out_size == 0U) return -1;
    out[0] = '\0';
    for (;;) {
        char* target = discard;
        uint32_t want = sizeof(discard);
        int rc;

        if (used + 1U < out_size) {
            target = out + used;
            want = (uint32_t)(out_size - used - 1U);
        }
        rc = fd_read(current, read_fd, target, want);
        if (rc < 0) return -1;
        if (rc == 0) break;
        if (target == out + used) {
            used += (size_t)rc;
            out[used] = '\0';
        }
    }
    return 0;
}

static int phase8_capture_external_output(const char* command_line, char* out, size_t out_size, int* out_status) {
    process_t* current = process_current();
    int pipefd[2] = { -1, -1 };
    int status = -1;

    if (!current || !command_line || !out || out_size == 0U) return -1;
    if (fd_dup2(current, 1, PHASE8_CAPTURE_TMP_STDOUT) < 0 ||
        fd_dup2(current, 2, PHASE8_CAPTURE_TMP_STDERR) < 0) {
        return -1;
    }
    if (fd_pipe(current, pipefd) != 0) goto done;
    if (fd_dup2(current, pipefd[1], 1) < 0 || fd_dup2(current, pipefd[1], 2) < 0) goto done;
    (void)fd_close(current, pipefd[1]);
    pipefd[1] = -1;

    status = shell_execute_external(command_line);

done:
    if (current->fd_table[PHASE8_CAPTURE_TMP_STDOUT]) {
        (void)fd_dup2(current, PHASE8_CAPTURE_TMP_STDOUT, 1);
        (void)fd_close(current, PHASE8_CAPTURE_TMP_STDOUT);
    }
    if (current->fd_table[PHASE8_CAPTURE_TMP_STDERR]) {
        (void)fd_dup2(current, PHASE8_CAPTURE_TMP_STDERR, 2);
        (void)fd_close(current, PHASE8_CAPTURE_TMP_STDERR);
    }
    if (pipefd[1] >= 0) (void)fd_close(current, pipefd[1]);
    if (pipefd[0] >= 0) {
        if (phase8_capture_pipe_output(current, pipefd[0], out, out_size) != 0) status = -1;
        (void)fd_close(current, pipefd[0]);
    } else {
        out[0] = '\0';
    }
    if (out_status) *out_status = status;
    return status >= 0 ? 0 : -1;
}

static int phase8_capture_pipeline_output(const char* left_line, const char* right_line,
                                          char* out, size_t out_size, int* out_status) {
    process_t* current = process_current();
    int pipefd[2] = { -1, -1 };
    int status = -1;

    if (!current || !left_line || !right_line || !out || out_size == 0U) return -1;
    if (fd_dup2(current, 1, PHASE8_CAPTURE_TMP_STDOUT) < 0 ||
        fd_dup2(current, 2, PHASE8_CAPTURE_TMP_STDERR) < 0) {
        return -1;
    }
    if (fd_pipe(current, pipefd) != 0) goto done;
    if (fd_dup2(current, pipefd[1], 1) < 0 || fd_dup2(current, pipefd[1], 2) < 0) goto done;
    (void)fd_close(current, pipefd[1]);
    pipefd[1] = -1;

    status = shell_execute_pipeline(left_line, right_line);

done:
    if (current->fd_table[PHASE8_CAPTURE_TMP_STDOUT]) {
        (void)fd_dup2(current, PHASE8_CAPTURE_TMP_STDOUT, 1);
        (void)fd_close(current, PHASE8_CAPTURE_TMP_STDOUT);
    }
    if (current->fd_table[PHASE8_CAPTURE_TMP_STDERR]) {
        (void)fd_dup2(current, PHASE8_CAPTURE_TMP_STDERR, 2);
        (void)fd_close(current, PHASE8_CAPTURE_TMP_STDERR);
    }
    if (pipefd[1] >= 0) (void)fd_close(current, pipefd[1]);
    if (pipefd[0] >= 0) {
        if (phase8_capture_pipe_output(current, pipefd[0], out, out_size) != 0) status = -1;
        (void)fd_close(current, pipefd[0]);
    } else {
        out[0] = '\0';
    }
    if (out_status) *out_status = status;
    return status >= 0 ? 0 : -1;
}

static int shell_execute_pipeline(const char* left_line, const char* right_line) {
    char left_argv_storage[PHASE8_SHELL_MAX_ARGS][PHASE8_SHELL_ARG_LEN];
    char right_argv_storage[PHASE8_SHELL_MAX_ARGS][PHASE8_SHELL_ARG_LEN];
    const char* left_argv[PHASE8_SHELL_MAX_ARGS];
    const char* right_argv[PHASE8_SHELL_MAX_ARGS];
    char left_path[128];
    char right_path[128];
    process_t* current = process_current();
    int pipefd[2] = { -1, -1 };
    int left_pid = -1;
    int right_pid = -1;
    int left_argc = 0;
    int right_argc = 0;
    int status = -1;

    if (!current) return -1;
    if (shell_parse_argv(left_line, left_argv_storage, left_argv, &left_argc) != 0) return -1;
    if (shell_parse_argv(right_line, right_argv_storage, right_argv, &right_argc) != 0) return -1;
    if (shell_resolve_path(left_argv[0], left_path, sizeof(left_path)) != 0 ||
        shell_resolve_path(right_argv[0], right_path, sizeof(right_path)) != 0) {
        vga_println("pipeline: command not found");
        return -1;
    }

    if (fd_dup2(current, 0, PHASE8_SHELL_TMP_STDIN) < 0 || fd_dup2(current, 1, PHASE8_SHELL_TMP_STDOUT) < 0) {
        vga_println("pipeline: fd snapshot failed");
        goto done;
    }
    if (fd_pipe(current, pipefd) != 0) {
        vga_println("pipeline: pipe creation failed");
        goto done;
    }

    if (fd_dup2(current, pipefd[1], 1) < 0) goto done;
    (void)fd_close(current, pipefd[1]);
    pipefd[1] = -1;
    left_pid = process_create_user(left_path, left_argv, left_argc, 0U);
    if (left_pid < 0) goto done;

    if (fd_dup2(current, PHASE8_SHELL_TMP_STDOUT, 1) < 0) goto done;
    (void)fd_close(current, PHASE8_SHELL_TMP_STDOUT);
    if (fd_dup2(current, pipefd[0], 0) < 0) goto done;
    (void)fd_close(current, pipefd[0]);
    pipefd[0] = -1;
    right_pid = process_create_user(right_path, right_argv, right_argc, 0U);
    if (right_pid < 0) goto done;

    if (fd_dup2(current, PHASE8_SHELL_TMP_STDIN, 0) < 0) goto done;
    (void)fd_close(current, PHASE8_SHELL_TMP_STDIN);
    status = shell_wait_for_child(left_pid, left_argv[0]);
    if (shell_wait_for_child(right_pid, right_argv[0]) != 0) status = -1;
    return status;

done:
    if (pipefd[0] >= 0) (void)fd_close(current, pipefd[0]);
    if (pipefd[1] >= 0) (void)fd_close(current, pipefd[1]);
    if (current) {
        if (current->fd_table[PHASE8_SHELL_TMP_STDOUT]) {
            (void)fd_dup2(current, PHASE8_SHELL_TMP_STDOUT, 1);
            (void)fd_close(current, PHASE8_SHELL_TMP_STDOUT);
        }
        if (current->fd_table[PHASE8_SHELL_TMP_STDIN]) {
            (void)fd_dup2(current, PHASE8_SHELL_TMP_STDIN, 0);
            (void)fd_close(current, PHASE8_SHELL_TMP_STDIN);
        }
    }
    if (left_pid > 0) (void)process_waitpid_sync_current(left_pid, 0U, 0);
    if (right_pid > 0) (void)process_waitpid_sync_current(right_pid, 0U, 0);
    vga_println("pipeline failed");
    return -1;
}

static void shell_builtin_pwd(void) {
    char path[256];

    fs_get_current_path(path, sizeof(path));
    vga_println(path);
}

static void shell_builtin_ls(void) {
    disk_fs_node_t entries[MAX_FILES];
    int count = fs_list_dir_entries(entries, MAX_FILES);

    if (count < 0) {
        vga_println("ls failed");
        return;
    }
    for (int i = 0; i < count; i++) {
        vga_print(entries[i].name);
        if (entries[i].flags == FS_NODE_DIR) vga_print("/");
        vga_println("");
    }
}

static void shell_execute_command(char* command) {
    const char* trimmed = shell_skip_spaces(command);
    int pipe_idx;

    if (!trimmed || trimmed[0] == '\0') return;
    if (strcmp(trimmed, "help") == 0) {
        shell_print_help();
        return;
    }
    if (strcmp(trimmed, "clear") == 0) {
        clear_screen();
        return;
    }
    if (strcmp(trimmed, "pwd") == 0) {
        shell_builtin_pwd();
        return;
    }
    if (strcmp(trimmed, "ls") == 0) {
        shell_builtin_ls();
        return;
    }
    if (strcmp(trimmed, "ver") == 0) {
        vga_println("NarcOs x86_64 Phase 10 native shell");
        return;
    }
    if (strncmp(trimmed, "cd ", 3) == 0) {
        if (fs_change_dir(shell_skip_spaces(trimmed + 3)) != 0) vga_println("cd failed");
        return;
    }
    if (strcmp(trimmed, "cd") == 0) {
        (void)fs_change_dir("/");
        return;
    }

    pipe_idx = shell_find_char(trimmed, '|');
    if (pipe_idx >= 0) {
        char left[128];
        char right[128];
        int right_off = 0;

        memset(left, 0, sizeof(left));
        memset(right, 0, sizeof(right));
        if ((size_t)pipe_idx >= sizeof(left)) {
            vga_println("pipeline too long");
            return;
        }
        memcpy(left, trimmed, (size_t)pipe_idx);
        for (const char* cursor = trimmed + pipe_idx + 1; *cursor != '\0' && right_off + 1 < (int)sizeof(right); cursor++) {
            right[right_off++] = *cursor;
        }
        if (shell_execute_pipeline(left, right) < 0) return;
        return;
    }

    (void)shell_execute_external(trimmed);
}

static void console_process_main(void) {
    for (;;) {
        if (cmd_ready) {
            shell_execute_command(cmd_to_execute);
            cmd_ready = 0;
            shell_print_prompt();
        }
        process_poll();
        asm volatile("hlt");
        process_poll();
    }
}

static void console_process_entry(void* arg) {
    (void)arg;
    console_process_main();
}

static void phase8_spawn_console(void) {
    if (process_create_kernel("console", console_process_entry, 0) < 0) {
        phase8_panic("console process creation failed");
    }

    (void)fs_change_dir("/");
    vga_println("ELF64 native userland is active.");
    shell_print_prompt();
    serial_write_line("[X64] Phase 10 shell ready");
}

static int phase8_run_kill_smoke(void) {
    static const char* child_argv[] = { "proc_test", "child", 0 };
    char output[64];
    char pid_text[16];
    char command[32];
    int child_pid;
    int kill_status = -1;
    int child_status = 0;
    size_t off = 0;

    child_pid = process_create_user("/bin/proc_test", child_argv, 2, 0U);
    if (child_pid < 0) return -1;

    strncpy(command, "kill ", sizeof(command) - 1U);
    command[sizeof(command) - 1U] = '\0';
    off = strlen(command);
    phase8_print_u32(pid_text, sizeof(pid_text), (uint32_t)child_pid);
    for (size_t i = 0; pid_text[i] != '\0' && off + 1U < sizeof(command); i++) {
        command[off++] = pid_text[i];
    }
    command[off] = '\0';

    if (phase8_capture_external_output(command, output, sizeof(output), &kill_status) != 0 || kill_status != 0) {
        return -1;
    }
    if (process_waitpid_sync_current(child_pid, 0U, &child_status) != child_pid || child_status != -1) return -1;
    return output[0] == '\0' ? 0 : -1;
}

static int phase8_smoke_large_file_roundtrip(void) {
    static const char path[] = "/home/user/Desktop/phase8-big.bin";
    const uint32_t total_size = 256U * 1024U;
    const uint32_t chunk_size = 4096U;
    uint8_t* write_buf = 0;
    uint8_t* read_buf = 0;
    uint32_t offset = 0U;
    int status = -1;

    write_buf = (uint8_t*)malloc(chunk_size);
    read_buf = (uint8_t*)malloc(chunk_size);
    if (!write_buf || !read_buf) goto cleanup;

    if (fs_write_file_raw(path, 0, 0U) < 0) goto cleanup;

    while (offset < total_size) {
        uint32_t chunk = total_size - offset;

        if (chunk > chunk_size) chunk = chunk_size;
        for (uint32_t i = 0; i < chunk; i++) {
            write_buf[i] = (uint8_t)(((offset + i) * 53U + 19U) & 0xFFU);
        }
        if (fs_write_file_raw_at(path, write_buf, offset, chunk) != (int)chunk) goto cleanup;
        offset += chunk;
    }

    offset = 0U;
    while (offset < total_size) {
        uint32_t chunk = total_size - offset;
        int read_len;

        if (chunk > chunk_size) chunk = chunk_size;
        read_len = fs_read_file_raw(path, read_buf, offset, chunk);
        if (read_len != (int)chunk) goto cleanup;
        for (uint32_t i = 0; i < chunk; i++) {
            uint8_t expected = (uint8_t)(((offset + i) * 53U + 19U) & 0xFFU);
            if (read_buf[i] != expected) goto cleanup;
        }
        offset += chunk;
    }

    status = 0;

cleanup:
    if (write_buf) free(write_buf);
    if (read_buf) free(read_buf);
    (void)fs_delete_file(path);
    return status;
}

static void phase8_smoke_process_main(void* arg) {
    static const char readme_text[] =
        "Welcome to NarcOs Professional Desktop!\n"
        "Files here appear on your desktop icons.\n";
    char output[1536];
    int command_status = -1;
    int all_passed = 1;

    (void)arg;
    serial_write_line("[X64] Phase 10 smoke start");
    vga_println("Running Phase 10 native smoke...");

    if (phase8_capture_external_output("hello", output, sizeof(output), &command_status) != 0 ||
        command_status != 0 || strcmp(output, "hello from /bin/hello v2\n") != 0) {
        all_passed = 0;
        phase8_log_smoke_result("hello", 0);
    } else {
        phase8_log_smoke_result("hello", 1);
    }

    if (phase8_capture_external_output("ps", output, sizeof(output), &command_status) != 0 ||
        command_status != 0 ||
        !phase8_text_contains(output, "PID\tPPID\tSTATE\tKIND\tNAME\tIMAGE\n") ||
        !phase8_text_contains(output, "/bin/ps")) {
        all_passed = 0;
        phase8_log_smoke_result("ps", 0);
    } else {
        phase8_log_smoke_result("ps", 1);
    }

    if (phase8_capture_external_output("echo phase8", output, sizeof(output), &command_status) != 0 ||
        command_status != 0 || strcmp(output, "phase8\n") != 0) {
        all_passed = 0;
        phase8_log_smoke_result("echo", 0);
    } else {
        phase8_log_smoke_result("echo", 1);
    }

    if (phase8_capture_external_output("cat /home/user/Desktop/readme.txt", output, sizeof(output), &command_status) != 0 ||
        command_status != 0 || strcmp(output, readme_text) != 0) {
        all_passed = 0;
        phase8_log_smoke_result("cat", 0);
    } else {
        phase8_log_smoke_result("cat", 1);
    }

    if (phase8_capture_external_output("proc_test", output, sizeof(output), &command_status) != 0 ||
        command_status != 0 || strcmp(output, "proc_test: ok\n") != 0) {
        all_passed = 0;
        phase8_log_smoke_result("proc_test", 0);
    } else {
        phase8_log_smoke_result("proc_test", 1);
    }

    if (phase8_capture_external_output("pipe_test", output, sizeof(output), &command_status) != 0 ||
        command_status != 0 || strcmp(output, "pipe_test: ok\n") != 0) {
        all_passed = 0;
        phase8_log_smoke_result("pipe_test", 0);
    } else {
        phase8_log_smoke_result("pipe_test", 1);
    }

    if (phase8_capture_pipeline_output("echo pipe-smoke", "cat", output, sizeof(output), &command_status) != 0 ||
        command_status != 0 || strcmp(output, "pipe-smoke\n") != 0) {
        all_passed = 0;
        phase8_log_smoke_result("shell-pipeline", 0);
    } else {
        phase8_log_smoke_result("shell-pipeline", 1);
    }

    if (phase8_run_kill_smoke() != 0) {
        all_passed = 0;
        phase8_log_smoke_result("kill", 0);
    } else {
        phase8_log_smoke_result("kill", 1);
    }

    if (phase8_smoke_large_file_roundtrip() != 0) {
        all_passed = 0;
        phase8_log_smoke_result("large-file", 0);
    } else {
        phase8_log_smoke_result("large-file", 1);
    }

    if (all_passed) {
        vga_println("Phase 10 native smoke: ok");
        serial_write_line("[X64] Phase 10 validation OK");
    } else {
        vga_println("Phase 10 native smoke: failed");
        serial_write_line("[X64] Phase 10 validation FAILED");
    }

    phase8_spawn_console();
    process_exit_current(all_passed ? 0 : 1);
}

void phase8_x86_64_main(void) {
    serial_init();
    serial_write_line("[X64] Phase 10 boot");

    arch_init_cpu();
    arch_init_legacy_pic();
    arch_init_timer(100U);
    arch_init_interrupts();
    arch_init_memory();
    init_heap();

    if (init_usermode() != 0) phase8_panic("usermode init failed");
    init_keyboard();
    storage_init();
    init_fs();
    rtc_init_timezone();
    read_rtc();
    process_init();

    x64_sti();
    vga_println("NarcOs x86_64 Phase 10");
    vga_println("System ready.");
    scheduler_start();
    phase8_halt_forever();
}
