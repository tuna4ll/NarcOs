#ifndef CPU_H
#define CPU_H

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
    char vendor[13];
} cpu_info_t;

void cpu_init();
const cpu_info_t* cpu_get_info();
int cpu_pse_supported();
int cpu_sse_enabled();

#endif
