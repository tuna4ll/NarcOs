#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include "gdt.h"

#define SYS_EXIT    0
#define SYS_PRINT   1
#define SYS_MALLOC  2
#define SYS_FREE    3
#define SYS_GUI_UPDATE 4
#define SYS_YIELD   5
#define SYS_UPTIME  6
#define SYS_GETPID  7
#define SYS_CHDIR   8
#define SYS_FS_READ 9
#define SYS_FS_WRITE 10
#define SYS_SNAKE_GET_INPUT 11
#define SYS_SNAKE_CLOSE 12
#define SYS_RANDOM 13
#define SYS_NET_GET_CONFIG 14
#define SYS_NET_RESOLVE 15
#define SYS_NET_NTP_QUERY 16
#define SYS_NET_SOCKET_OPEN 17
#define SYS_NET_SOCKET_CONNECT 18
#define SYS_NET_SOCKET_SEND 19
#define SYS_NET_SOCKET_RECV 20
#define SYS_NET_SOCKET_AVAILABLE 21
#define SYS_NET_SOCKET_CLOSE 22

void init_syscalls();
void syscall_handler(trap_frame_t* frame);

#endif
