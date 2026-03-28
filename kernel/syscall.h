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

void init_syscalls();
void syscall_handler(trap_frame_t* frame);

#endif
