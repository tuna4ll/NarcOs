#ifndef SYSCALL_H
#define SYSCALL_H

#include <stddef.h>
#include <stdint.h>
#include "gdt.h"
#include "process_api.h"

#define PRIV_CMD_SNAKE 1
#define PRIV_CMD_SETTINGS 2
#define PRIV_CMD_EDIT 3
#define PRIV_CMD_MEM 4
#define PRIV_CMD_MALLOC_TEST 5
#define PRIV_CMD_USERMODE_TEST 6
#define PRIV_CMD_HWINFO 7
#define PRIV_CMD_PCI 8
#define PRIV_CMD_STORAGE 9
#define PRIV_CMD_LOG 10
#define PRIV_CMD_REBOOT 11
#define PRIV_CMD_POWEROFF 12
#define PRIV_CMD_PROC_DUMP 13
#define PRIV_CMD_PROC_TEST 14
#define PRIV_CMD_PIPE_TEST 15

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} rtc_local_time_t;

#define WAITPID_FLAG_NOHANG 0x1U

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
#define SYS_FS_LIST 23
#define SYS_FS_GET_CWD 24
#define SYS_FS_TOUCH 25
#define SYS_FS_MKDIR 26
#define SYS_FS_DELETE 27
#define SYS_FS_MOVE 28
#define SYS_FS_RENAME 29
#define SYS_CLEAR_SCREEN 30
#define SYS_RTC_GET_LOCAL 31
#define SYS_NET_DHCP 32
#define SYS_NET_PING 33
#define SYS_PRIV_CMD 34
#define SYS_PRINT_RAW 35
#define SYS_FS_FIND_NODE 36
#define SYS_FS_GET_NODE_INFO 37
#define SYS_FS_GET_PATH 38
#define SYS_RTC_GET_TZ_OFFSET 39
#define SYS_RTC_SET_TZ_OFFSET 40
#define SYS_RTC_SAVE_TZ 41
#define SYS_GUI_OPEN_NARCPAD_FILE 42
#define SYS_GETRANDOM 43
#define SYS_FS_READ_RAW 44
#define SYS_FS_WRITE_RAW 45
#define SYS_SPAWN 46
#define SYS_EXEC 47
#define SYS_WAITPID 48
#define SYS_KILL 49
#define SYS_GETPPID 50
#define SYS_SLEEP 51
#define SYS_READ 52
#define SYS_WRITE 53
#define SYS_CLOSE 54
#define SYS_DUP2 55
#define SYS_PIPE 56
#define SYS_PROCESS_SNAPSHOT 57

void init_syscalls();
void syscall_handler(trap_frame_t* frame);
int copy_from_user(void* dst, const void* user_src, uint32_t len);
int copy_to_user(void* user_dst, const void* src, uint32_t len);
int copy_string_from_user(char* dst, const char* user_src, size_t dst_size);

#endif
