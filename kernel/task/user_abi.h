#ifndef USER_ABI_H
#define USER_ABI_H

#include <stdint.h>
#include "fs.h"
#include "net.h"
#include "syscall.h"

static inline int user_syscall0(int num) {
    int ret;
    asm volatile("int $0x80"
                 : "=a"(ret)
                 : "a"(num)
                 : "memory");
    return ret;
}

static inline int user_syscall1(int num, uint32_t arg1) {
    int ret;
    asm volatile("int $0x80"
                 : "=a"(ret)
                 : "a"(num), "b"(arg1)
                 : "memory");
    return ret;
}

static inline int user_syscall2(int num, uint32_t arg1, uint32_t arg2) {
    int ret;
    asm volatile("int $0x80"
                 : "=a"(ret)
                 : "a"(num), "b"(arg1), "c"(arg2)
                 : "memory");
    return ret;
}

static inline int user_syscall3(int num, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    int ret;
    asm volatile("int $0x80"
                 : "=a"(ret)
                 : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3)
                 : "memory");
    return ret;
}

static inline int user_syscall4(int num, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4) {
    int ret;
    asm volatile("int $0x80"
                 : "=a"(ret)
                 : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3), "S"(arg4)
                 : "memory");
    return ret;
}

static inline void user_yield(void) {
    asm volatile("int $0x81" ::: "memory");
}

static inline void user_print(const char* text) {
    (void)user_syscall1(SYS_PRINT, (uint32_t)text);
}

static inline void user_print_raw(const char* text) {
    (void)user_syscall1(SYS_PRINT_RAW, (uint32_t)text);
}

static inline uint32_t user_uptime_ticks(void) {
    return (uint32_t)user_syscall0(SYS_UPTIME);
}

static inline int user_fs_read(const char* path, char* buffer, uint32_t max_len) {
    return user_syscall3(SYS_FS_READ, (uint32_t)path, (uint32_t)buffer, max_len);
}

static inline int user_fs_write(const char* path, const char* contents) {
    return user_syscall2(SYS_FS_WRITE, (uint32_t)path, (uint32_t)contents);
}

static inline int user_fs_list(disk_fs_node_t* entries, int max_entries) {
    return user_syscall2(SYS_FS_LIST, (uint32_t)entries, (uint32_t)max_entries);
}

static inline int user_fs_get_cwd(char* path, uint32_t max_len) {
    return user_syscall2(SYS_FS_GET_CWD, (uint32_t)path, max_len);
}

static inline int user_fs_touch(const char* path) {
    return user_syscall1(SYS_FS_TOUCH, (uint32_t)path);
}

static inline int user_fs_mkdir(const char* path) {
    return user_syscall1(SYS_FS_MKDIR, (uint32_t)path);
}

static inline int user_fs_delete(const char* path) {
    return user_syscall1(SYS_FS_DELETE, (uint32_t)path);
}

static inline int user_fs_move(const char* src, const char* target_dir) {
    return user_syscall2(SYS_FS_MOVE, (uint32_t)src, (uint32_t)target_dir);
}

static inline int user_fs_rename(const char* path, const char* new_name) {
    return user_syscall2(SYS_FS_RENAME, (uint32_t)path, (uint32_t)new_name);
}

static inline int user_fs_find_node(const char* path) {
    return user_syscall1(SYS_FS_FIND_NODE, (uint32_t)path);
}

static inline int user_fs_get_node_info(int idx, disk_fs_node_t* out_node) {
    return user_syscall2(SYS_FS_GET_NODE_INFO, (uint32_t)idx, (uint32_t)out_node);
}

static inline int user_fs_get_path(int idx, char* path, uint32_t max_len) {
    return user_syscall3(SYS_FS_GET_PATH, (uint32_t)idx, (uint32_t)path, max_len);
}

static inline void user_clear_screen(void) {
    (void)user_syscall0(SYS_CLEAR_SCREEN);
}

static inline int user_get_local_time(rtc_local_time_t* out_time) {
    return user_syscall1(SYS_RTC_GET_LOCAL, (uint32_t)out_time);
}

static inline int user_get_timezone_offset_minutes(void) {
    return user_syscall0(SYS_RTC_GET_TZ_OFFSET);
}

static inline int user_set_timezone_offset_minutes(int minutes) {
    return user_syscall1(SYS_RTC_SET_TZ_OFFSET, (uint32_t)minutes);
}

static inline int user_save_timezone_setting(void) {
    return user_syscall0(SYS_RTC_SAVE_TZ);
}

static inline int user_net_get_config(net_ipv4_config_t* out_config) {
    return user_syscall1(SYS_NET_GET_CONFIG, (uint32_t)out_config);
}

static inline int user_net_dhcp(void) {
    return user_syscall0(SYS_NET_DHCP);
}

static inline int user_net_resolve(const char* host, uint32_t* out_ip) {
    return user_syscall2(SYS_NET_RESOLVE, (uint32_t)host, (uint32_t)out_ip);
}

static inline int user_net_ntp_query(const char* host, uint32_t* out_unix_seconds) {
    return user_syscall2(SYS_NET_NTP_QUERY, (uint32_t)host, (uint32_t)out_unix_seconds);
}

static inline int user_net_ping(const char* host, net_ping_result_t* out_result) {
    return user_syscall2(SYS_NET_PING, (uint32_t)host, (uint32_t)out_result);
}

static inline int user_net_socket(int type) {
    return user_syscall1(SYS_NET_SOCKET_OPEN, (uint32_t)type);
}

static inline int user_net_connect(int handle, uint32_t remote_ip, uint16_t port, uint32_t timeout_ticks) {
    return user_syscall4(SYS_NET_SOCKET_CONNECT, (uint32_t)handle, remote_ip, (uint32_t)port, timeout_ticks);
}

static inline int user_net_send(int handle, const void* data, uint16_t length) {
    return user_syscall3(SYS_NET_SOCKET_SEND, (uint32_t)handle, (uint32_t)data, (uint32_t)length);
}

static inline int user_net_recv(int handle, void* data, uint16_t length) {
    return user_syscall3(SYS_NET_SOCKET_RECV, (uint32_t)handle, (uint32_t)data, (uint32_t)length);
}

static inline int user_net_available(int handle) {
    return user_syscall1(SYS_NET_SOCKET_AVAILABLE, (uint32_t)handle);
}

static inline int user_net_close(int handle) {
    return user_syscall1(SYS_NET_SOCKET_CLOSE, (uint32_t)handle);
}

static inline int user_priv_command(int cmd, const char* arg) {
    return user_syscall2(SYS_PRIV_CMD, (uint32_t)cmd, (uint32_t)arg);
}

static inline int user_gui_open_narcpad_file(const char* path) {
    return user_syscall1(SYS_GUI_OPEN_NARCPAD_FILE, (uint32_t)path);
}

#endif
