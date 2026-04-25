#ifndef USER_ABI_H
#define USER_ABI_H

#include <stddef.h>
#include <stdint.h>
#include "fs.h"
#include "net.h"
#include "syscall.h"

typedef intptr_t user_syscall_ret_t;

static inline user_syscall_ret_t user_syscall0(int num) {
    user_syscall_ret_t ret;
    asm volatile("int $0x80"
                 : "=a"(ret)
                 : "a"((uintptr_t)num)
                 : "memory");
    return ret;
}

static inline user_syscall_ret_t user_syscall1(int num, uintptr_t arg1) {
    user_syscall_ret_t ret;
    asm volatile("int $0x80"
                 : "=a"(ret)
                 : "a"((uintptr_t)num), "b"(arg1)
                 : "memory");
    return ret;
}

static inline user_syscall_ret_t user_syscall2(int num, uintptr_t arg1, uintptr_t arg2) {
    user_syscall_ret_t ret;
    asm volatile("int $0x80"
                 : "=a"(ret)
                 : "a"((uintptr_t)num), "b"(arg1), "c"(arg2)
                 : "memory");
    return ret;
}

static inline user_syscall_ret_t user_syscall3(int num, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3) {
    user_syscall_ret_t ret;
    asm volatile("int $0x80"
                 : "=a"(ret)
                 : "a"((uintptr_t)num), "b"(arg1), "c"(arg2), "d"(arg3)
                 : "memory");
    return ret;
}

static inline user_syscall_ret_t user_syscall4(int num, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3, uintptr_t arg4) {
    user_syscall_ret_t ret;
    asm volatile("int $0x80"
                 : "=a"(ret)
                 : "a"((uintptr_t)num), "b"(arg1), "c"(arg2), "d"(arg3), "S"(arg4)
                 : "memory");
    return ret;
}

static inline void user_yield(void) {
    asm volatile("int $0x81" ::: "memory");
}

static inline void user_exit(int code) {
    (void)user_syscall1(SYS_EXIT, (uintptr_t)code);
    for (;;) {
        user_yield();
    }
}

static inline void user_print(const char* text) {
    (void)user_syscall1(SYS_PRINT, (uintptr_t)text);
}

static inline void user_print_raw(const char* text) {
    (void)user_syscall1(SYS_PRINT_RAW, (uintptr_t)text);
}

static inline uint32_t user_uptime_ticks(void) {
    return (uint32_t)user_syscall0(SYS_UPTIME);
}

static inline int user_getpid(void) {
    return (int)user_syscall0(SYS_GETPID);
}

static inline int user_getppid(void) {
    return (int)user_syscall0(SYS_GETPPID);
}

static inline int user_spawn(const char* path, const char* const* argv, uint32_t argc) {
    return (int)user_syscall3(SYS_SPAWN, (uintptr_t)path, (uintptr_t)argv, argc);
}

static inline int user_exec(const char* path, const char* const* argv, uint32_t argc) {
    return (int)user_syscall3(SYS_EXEC, (uintptr_t)path, (uintptr_t)argv, argc);
}

static inline int user_waitpid(int pid, int* out_status, uint32_t flags) {
    return (int)user_syscall3(SYS_WAITPID, (uintptr_t)pid, (uintptr_t)out_status, flags);
}

static inline int user_kill(int pid) {
    return (int)user_syscall1(SYS_KILL, (uintptr_t)pid);
}

static inline int user_sleep(uint32_t ticks) {
    return (int)user_syscall1(SYS_SLEEP, ticks);
}

static inline int user_read(int fd, void* buffer, uint32_t len) {
    return (int)user_syscall3(SYS_READ, (uintptr_t)fd, (uintptr_t)buffer, len);
}

static inline int user_write(int fd, const void* buffer, uint32_t len) {
    return (int)user_syscall3(SYS_WRITE, (uintptr_t)fd, (uintptr_t)buffer, len);
}

static inline int user_close(int fd) {
    return (int)user_syscall1(SYS_CLOSE, (uintptr_t)fd);
}

static inline int user_dup2(int oldfd, int newfd) {
    return (int)user_syscall2(SYS_DUP2, (uintptr_t)oldfd, (uintptr_t)newfd);
}

static inline int user_pipe(int out_fds[2]) {
    return (int)user_syscall1(SYS_PIPE, (uintptr_t)out_fds);
}

static inline int user_process_snapshot(process_snapshot_entry_t* entries, int max_entries) {
    return (int)user_syscall2(SYS_PROCESS_SNAPSHOT, (uintptr_t)entries, (uintptr_t)max_entries);
}

static inline void* user_malloc(size_t size) {
    return (void*)(uintptr_t)user_syscall1(SYS_MALLOC, (uintptr_t)size);
}

static inline void user_free(void* ptr) {
    (void)user_syscall1(SYS_FREE, (uintptr_t)ptr);
}

static inline int user_getrandom(void* buf, uint32_t len) {
    return (int)user_syscall2(SYS_GETRANDOM, (uintptr_t)buf, len);
}

static inline int user_fs_read(const char* path, char* buffer, uint32_t max_len) {
    return (int)user_syscall3(SYS_FS_READ, (uintptr_t)path, (uintptr_t)buffer, max_len);
}

static inline int user_fs_write(const char* path, const char* contents) {
    return (int)user_syscall2(SYS_FS_WRITE, (uintptr_t)path, (uintptr_t)contents);
}

static inline int user_fs_read_raw(const char* path, void* buffer, uint32_t max_len, uint32_t offset) {
    return (int)user_syscall4(SYS_FS_READ_RAW, (uintptr_t)path, (uintptr_t)buffer, max_len, offset);
}

static inline int user_fs_write_raw(const char* path, const void* contents, uint32_t len) {
    return (int)user_syscall3(SYS_FS_WRITE_RAW, (uintptr_t)path, (uintptr_t)contents, len);
}

static inline int user_fs_list(disk_fs_node_t* entries, int max_entries) {
    return (int)user_syscall2(SYS_FS_LIST, (uintptr_t)entries, (uintptr_t)max_entries);
}

static inline int user_fs_get_cwd(char* path, uint32_t max_len) {
    return (int)user_syscall2(SYS_FS_GET_CWD, (uintptr_t)path, max_len);
}

static inline int user_fs_touch(const char* path) {
    return (int)user_syscall1(SYS_FS_TOUCH, (uintptr_t)path);
}

static inline int user_fs_mkdir(const char* path) {
    return (int)user_syscall1(SYS_FS_MKDIR, (uintptr_t)path);
}

static inline int user_fs_delete(const char* path) {
    return (int)user_syscall1(SYS_FS_DELETE, (uintptr_t)path);
}

static inline int user_fs_move(const char* src, const char* target_dir) {
    return (int)user_syscall2(SYS_FS_MOVE, (uintptr_t)src, (uintptr_t)target_dir);
}

static inline int user_fs_rename(const char* path, const char* new_name) {
    return (int)user_syscall2(SYS_FS_RENAME, (uintptr_t)path, (uintptr_t)new_name);
}

static inline int user_fs_find_node(const char* path) {
    return (int)user_syscall1(SYS_FS_FIND_NODE, (uintptr_t)path);
}

static inline int user_fs_get_node_info(int idx, disk_fs_node_t* out_node) {
    return (int)user_syscall2(SYS_FS_GET_NODE_INFO, (uintptr_t)idx, (uintptr_t)out_node);
}

static inline int user_fs_get_path(int idx, char* path, uint32_t max_len) {
    return (int)user_syscall3(SYS_FS_GET_PATH, (uintptr_t)idx, (uintptr_t)path, max_len);
}

static inline int user_snake_get_input(void) {
    return (int)user_syscall0(SYS_SNAKE_GET_INPUT);
}

static inline int user_snake_close(void) {
    return (int)user_syscall0(SYS_SNAKE_CLOSE);
}

static inline uint32_t user_random_u32(void) {
    return (uint32_t)user_syscall0(SYS_RANDOM);
}

static inline void user_clear_screen(void) {
    (void)user_syscall0(SYS_CLEAR_SCREEN);
}

static inline int user_get_local_time(rtc_local_time_t* out_time) {
    return (int)user_syscall1(SYS_RTC_GET_LOCAL, (uintptr_t)out_time);
}

static inline int user_get_timezone_offset_minutes(void) {
    return user_syscall0(SYS_RTC_GET_TZ_OFFSET);
}

static inline int user_set_timezone_offset_minutes(int minutes) {
    return (int)user_syscall1(SYS_RTC_SET_TZ_OFFSET, (uintptr_t)minutes);
}

static inline int user_save_timezone_setting(void) {
    return user_syscall0(SYS_RTC_SAVE_TZ);
}

static inline int user_net_get_config(net_ipv4_config_t* out_config) {
    return (int)user_syscall1(SYS_NET_GET_CONFIG, (uintptr_t)out_config);
}

static inline int user_net_dhcp(void) {
    return user_syscall0(SYS_NET_DHCP);
}

static inline int user_net_resolve(const char* host, uint32_t* out_ip) {
    return (int)user_syscall2(SYS_NET_RESOLVE, (uintptr_t)host, (uintptr_t)out_ip);
}

static inline int user_net_ntp_query(const char* host, uint32_t* out_unix_seconds) {
    return (int)user_syscall2(SYS_NET_NTP_QUERY, (uintptr_t)host, (uintptr_t)out_unix_seconds);
}

static inline int user_net_ping(const char* host, net_ping_result_t* out_result) {
    return (int)user_syscall2(SYS_NET_PING, (uintptr_t)host, (uintptr_t)out_result);
}

static inline int user_net_socket(int type) {
    return (int)user_syscall1(SYS_NET_SOCKET_OPEN, (uintptr_t)type);
}

static inline int user_net_connect(int handle, uint32_t remote_ip, uint16_t port, uint32_t timeout_ticks) {
    return (int)user_syscall4(SYS_NET_SOCKET_CONNECT, (uintptr_t)handle, remote_ip, port, timeout_ticks);
}

static inline int user_net_send(int handle, const void* data, uint16_t length) {
    return (int)user_syscall3(SYS_NET_SOCKET_SEND, (uintptr_t)handle, (uintptr_t)data, length);
}

static inline int user_net_recv(int handle, void* data, uint16_t length) {
    return (int)user_syscall3(SYS_NET_SOCKET_RECV, (uintptr_t)handle, (uintptr_t)data, length);
}

static inline int user_net_available(int handle) {
    return (int)user_syscall1(SYS_NET_SOCKET_AVAILABLE, (uintptr_t)handle);
}

static inline int user_net_close(int handle) {
    return (int)user_syscall1(SYS_NET_SOCKET_CLOSE, (uintptr_t)handle);
}

static inline int user_priv_command(int cmd, const char* arg) {
    return (int)user_syscall2(SYS_PRIV_CMD, (uintptr_t)cmd, (uintptr_t)arg);
}

static inline int user_gui_open_narcpad_file(const char* path) {
    return (int)user_syscall1(SYS_GUI_OPEN_NARCPAD_FILE, (uintptr_t)path);
}

#endif
