#include "cpu.h"
#include "serial.h"
#include "string.h"

static cpu_info_t cpu_info;

static int cpu_has_cpuid() {
    uint32_t original_flags;
    uint32_t toggled_flags;

    asm volatile(
        "pushfl\n\t"
        "popl %0\n\t"
        "movl %0, %1\n\t"
        "xorl $0x00200000, %1\n\t"
        "pushl %1\n\t"
        "popfl\n\t"
        "pushfl\n\t"
        "popl %1\n\t"
        "pushl %0\n\t"
        "popfl"
        : "=&r"(original_flags), "=&r"(toggled_flags)
        :
        : "cc"
    );

    return ((original_flags ^ toggled_flags) & 0x00200000U) != 0U;
}

static void cpu_cpuid(uint32_t leaf, uint32_t* eax, uint32_t* ebx, uint32_t* ecx, uint32_t* edx) {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;

    asm volatile(
        "cpuid"
        : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
        : "a"(leaf), "c"(0)
        : "cc"
    );

    if (eax) *eax = a;
    if (ebx) *ebx = b;
    if (ecx) *ecx = c;
    if (edx) *edx = d;
}

static void cpu_enable_sse() {
    uint32_t cr0;
    uint32_t cr4;
    static const uint32_t default_mxcsr = 0x1F80U;

    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1U << 2);
    cr0 |= (1U << 1);
    asm volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");

    asm volatile("clts");

    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1U << 9) | (1U << 10);
    asm volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");

    asm volatile("fninit");
    asm volatile("ldmxcsr %0" : : "m"(default_mxcsr));
}

static void cpu_log_info() {
    serial_write("[cpu] vendor=");
    serial_write(cpu_info.vendor);
    serial_write(" cpuid=");
    serial_write_char(cpu_info.cpuid_supported ? '1' : '0');
    serial_write(" pse=");
    serial_write_char(cpu_info.pse_supported ? '1' : '0');
    serial_write(" sse=");
    serial_write_char(cpu_info.sse_supported ? '1' : '0');
    serial_write(" fxsr=");
    serial_write_char(cpu_info.fxsr_supported ? '1' : '0');
    serial_write(" sse_on=");
    serial_write_char(cpu_info.sse_enabled ? '1' : '0');
    serial_write(" apic=");
    serial_write_char(cpu_info.apic_supported ? '1' : '0');
    serial_write(" tsc=");
    serial_write_char(cpu_info.tsc_supported ? '1' : '0');
    serial_write_char('\n');
}

void cpu_init() {
    uint32_t eax = 0;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;

    memset(&cpu_info, 0, sizeof(cpu_info));
    strcpy(cpu_info.vendor, "unknown");
    cpu_info.cpuid_supported = cpu_has_cpuid();
    if (!cpu_info.cpuid_supported) {
        serial_write_line("[cpu] CPUID unsupported, SSE disabled.");
        return;
    }

    cpu_cpuid(0, &eax, &ebx, &ecx, &edx);
    cpu_info.max_basic_leaf = eax;
    memcpy(&cpu_info.vendor[0], &ebx, sizeof(uint32_t));
    memcpy(&cpu_info.vendor[4], &edx, sizeof(uint32_t));
    memcpy(&cpu_info.vendor[8], &ecx, sizeof(uint32_t));
    cpu_info.vendor[12] = '\0';

    if (cpu_info.max_basic_leaf >= 1U) {
        cpu_cpuid(1, &eax, &ebx, &ecx, &edx);
        cpu_info.pse_supported = (edx & (1U << 3)) != 0U;
        cpu_info.tsc_supported = (edx & (1U << 4)) != 0U;
        cpu_info.apic_supported = (edx & (1U << 9)) != 0U;
        cpu_info.fxsr_supported = (edx & (1U << 24)) != 0U;
        cpu_info.sse_supported = (edx & (1U << 25)) != 0U;
    }

    if (cpu_info.sse_supported && cpu_info.fxsr_supported) {
        cpu_enable_sse();
        cpu_info.sse_enabled = 1;
    } else {
        serial_write_line("[cpu] SSE unavailable, renderer will use scalar fallback.");
    }

    cpu_log_info();
}

const cpu_info_t* cpu_get_info() {
    return &cpu_info;
}

int cpu_pse_supported() {
    return cpu_info.pse_supported;
}

int cpu_sse_enabled() {
    return cpu_info.sse_enabled;
}
