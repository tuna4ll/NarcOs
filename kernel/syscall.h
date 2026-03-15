#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include "gdt.h"

#define SYS_EXIT    0
#define SYS_PRINT   1
#define SYS_MALLOC  2
#define SYS_FREE    3
#define SYS_GUI_UPDATE 4

void init_syscalls();
void syscall_handler(trap_frame_t* frame);

#endif
