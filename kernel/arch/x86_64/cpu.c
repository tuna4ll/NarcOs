#include "cpu.h"
#include "x64_serial.h"

static x64_cpu_info_t cpu_info;

static void x64_cpuid(uint32_t leaf, uint32_t subleaf,
                      uint32_t* eax, uint32_t* ebx, uint32_t* ecx, uint32_t* edx) {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;

    __asm__ volatile(
        "cpuid"
        : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
        : "a"(leaf), "c"(subleaf)
        : "cc"
    );

    if (eax) *eax = a;
    if (ebx) *ebx = b;
    if (ecx) *ecx = c;
    if (edx) *edx = d;
}

void x64_cpu_init(void) {
    uint32_t eax = 0;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;

    cpu_info.cpuid_supported = 1;
    cpu_info.pse_supported = 0;
    cpu_info.sse_supported = 0;
    cpu_info.fxsr_supported = 0;
    cpu_info.apic_supported = 0;
    cpu_info.tsc_supported = 0;
    cpu_info.sse_enabled = 0;
    cpu_info.long_mode_supported = 0;
    cpu_info.vendor[0] = '\0';

    x64_cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    cpu_info.max_basic_leaf = eax;
    ((uint32_t*)&cpu_info.vendor[0])[0] = ebx;
    ((uint32_t*)&cpu_info.vendor[0])[1] = edx;
    ((uint32_t*)&cpu_info.vendor[0])[2] = ecx;
    cpu_info.vendor[12] = '\0';

    if (cpu_info.max_basic_leaf >= 1U) {
        x64_cpuid(1U, 0U, &eax, &ebx, &ecx, &edx);
        cpu_info.pse_supported = (int)((edx >> 3) & 0x1U);
        cpu_info.tsc_supported = (int)((edx >> 4) & 0x1U);
        cpu_info.apic_supported = (int)((edx >> 9) & 0x1U);
        cpu_info.fxsr_supported = (int)((edx >> 24) & 0x1U);
        cpu_info.sse_supported = (int)((edx >> 25) & 0x1U);
        cpu_info.sse_enabled = cpu_info.sse_supported;
    }

    x64_cpuid(0x80000000U, 0, &eax, &ebx, &ecx, &edx);
    cpu_info.max_extended_leaf = eax;
    if (eax >= 0x80000001U) {
        x64_cpuid(0x80000001U, 0, &eax, &ebx, &ecx, &edx);
        cpu_info.long_mode_supported = (uint8_t)((edx >> 29) & 0x1U);
    }

    x64_serial_write("[cpu64] vendor=");
    x64_serial_write(cpu_info.vendor);
    x64_serial_write(" max_basic=");
    x64_serial_write_hex32(cpu_info.max_basic_leaf);
    x64_serial_write(" max_ext=");
    x64_serial_write_hex32(cpu_info.max_extended_leaf);
    x64_serial_write(" long_mode=");
    x64_serial_write(cpu_info.long_mode_supported ? "1" : "0");
    x64_serial_write_char('\n');
}

const x64_cpu_info_t* x64_cpu_get_info(void) {
    return &cpu_info;
}

void cpu_init(void) {
    x64_cpu_init();
}

const cpu_info_t* cpu_get_info(void) {
    return &cpu_info;
}

int cpu_pse_supported(void) {
    return cpu_info.pse_supported;
}

int cpu_sse_enabled(void) {
    return cpu_info.sse_enabled;
}

uint8_t x64_inb(uint16_t port) {
    uint8_t value;

    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void x64_outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

uint64_t x64_read_cr2(void) {
    uint64_t value;

    __asm__ volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

void x64_hlt(void) {
    __asm__ volatile("hlt");
}

void x64_cli(void) {
    __asm__ volatile("cli" : : : "memory");
}

void x64_sti(void) {
    __asm__ volatile("sti" : : : "memory");
}
