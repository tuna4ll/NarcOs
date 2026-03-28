#ifndef USER_ABI_H
#define USER_ABI_H

#include <stdint.h>
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

static inline uint32_t user_uptime_ticks(void) {
    return (uint32_t)user_syscall0(SYS_UPTIME);
}

static inline int user_fs_write(const char* path, const char* contents) {
    return user_syscall2(SYS_FS_WRITE, (uint32_t)path, (uint32_t)contents);
}

static inline int user_net_resolve(const char* host, uint32_t* out_ip) {
    return user_syscall2(SYS_NET_RESOLVE, (uint32_t)host, (uint32_t)out_ip);
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

#endif
