#ifndef NARCOS_X86_64_CPU_H
#define NARCOS_X86_64_CPU_H

#include <stdint.h>

typedef struct {
    int cpuid_supported;
    int pse_supported;
    int sse_supported;
    int fxsr_supported;
    int apic_supported;
    int tsc_supported;
    int sse_enabled;
    uint32_t max_basic_leaf;
    uint32_t max_extended_leaf;
    uint8_t long_mode_supported;
    char vendor[13];
} cpu_info_t;

typedef cpu_info_t x64_cpu_info_t;

void x64_cpu_init(void);
const x64_cpu_info_t* x64_cpu_get_info(void);
void cpu_init(void);
const cpu_info_t* cpu_get_info(void);
int cpu_pse_supported(void);
int cpu_sse_enabled(void);

uint8_t x64_inb(uint16_t port);
void x64_outb(uint16_t port, uint8_t value);
uint64_t x64_read_cr2(void);
void x64_hlt(void);
void x64_cli(void);
void x64_sti(void);

#endif
