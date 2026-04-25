#ifndef NARCOS_X86_64_GDT_H
#define NARCOS_X86_64_GDT_H

#include <stdint.h>

#define X64_GDT_LEGACY_CODE 0x08
#define X64_GDT_KERNEL_DATA 0x10
#define X64_GDT_KERNEL_CODE 0x18
#define X64_GDT_USER_DATA   0x20
#define X64_GDT_USER_CODE32 0x28
#define X64_GDT_USER_CODE64 0x30
#define X64_GDT_TSS         0x38

void x64_gdt_init(void);
void x64_gdt_set_kernel_stack(uint64_t stack_top);

#endif
