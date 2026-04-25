#include "gdt.h"
#include "x64_serial.h"

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed)) x64_tss_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) x64_gdtr_t;

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid0;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_mid1;
    uint32_t base_high;
    uint32_t reserved;
} __attribute__((packed)) x64_tss_descriptor_t;

typedef struct {
    uint64_t null_descriptor;
    uint64_t legacy_code_descriptor;
    uint64_t kernel_data_descriptor;
    uint64_t kernel_code_descriptor;
    uint64_t user_data_descriptor;
    uint64_t user_code32_descriptor;
    uint64_t user_code64_descriptor;
    x64_tss_descriptor_t tss_descriptor;
} __attribute__((packed)) x64_gdt_table_t;

static x64_gdt_table_t gdt_table;
static x64_gdtr_t gdtr;
static x64_tss_t tss;

static uint8_t boot_rsp0_stack[8192] __attribute__((aligned(16)));
static uint8_t double_fault_ist_stack[8192] __attribute__((aligned(16)));

static void x64_gdt_set_tss_descriptor(x64_tss_descriptor_t* descriptor, uint64_t base, uint32_t limit) {
    descriptor->limit_low = (uint16_t)(limit & 0xFFFFU);
    descriptor->base_low = (uint16_t)(base & 0xFFFFU);
    descriptor->base_mid0 = (uint8_t)((base >> 16) & 0xFFU);
    descriptor->access = 0x89U;
    descriptor->granularity = (uint8_t)((limit >> 16) & 0x0FU);
    descriptor->base_mid1 = (uint8_t)((base >> 24) & 0xFFU);
    descriptor->base_high = (uint32_t)((base >> 32) & 0xFFFFFFFFU);
    descriptor->reserved = 0;
}

void x64_gdt_init(void) {
    uint64_t tss_base = (uint64_t)(uintptr_t)&tss;
    uint32_t tss_limit = (uint32_t)(sizeof(tss) - 1U);
    uint16_t data_selector = X64_GDT_KERNEL_DATA;
    uint16_t tss_selector = X64_GDT_TSS;

    gdt_table.null_descriptor = 0x0000000000000000ULL;
    gdt_table.legacy_code_descriptor = 0x00CF9A000000FFFFULL;
    gdt_table.kernel_data_descriptor = 0x00CF92000000FFFFULL;
    gdt_table.kernel_code_descriptor = 0x00AF9A000000FFFFULL;
    gdt_table.user_data_descriptor = 0x00CFF2000000FFFFULL;
    gdt_table.user_code32_descriptor = 0x00CFFA000000FFFFULL;
    gdt_table.user_code64_descriptor = 0x00AFFA000000FFFFULL;
    x64_gdt_set_tss_descriptor(&gdt_table.tss_descriptor, tss_base, tss_limit);

    tss.reserved0 = 0;
    tss.rsp0 = (uint64_t)(uintptr_t)(boot_rsp0_stack + sizeof(boot_rsp0_stack));
    tss.rsp1 = 0;
    tss.rsp2 = 0;
    tss.reserved1 = 0;
    tss.ist1 = (uint64_t)(uintptr_t)(double_fault_ist_stack + sizeof(double_fault_ist_stack));
    tss.ist2 = 0;
    tss.ist3 = 0;
    tss.ist4 = 0;
    tss.ist5 = 0;
    tss.ist6 = 0;
    tss.ist7 = 0;
    tss.reserved2 = 0;
    tss.reserved3 = 0;
    tss.iomap_base = sizeof(tss);

    gdtr.limit = (uint16_t)(sizeof(gdt_table) - 1U);
    gdtr.base = (uint64_t)(uintptr_t)&gdt_table;

    x64_serial_write_line("[gdt64] lgdt");
    __asm__ volatile("lgdt %0" : : "m"(gdtr) : "memory");
    x64_serial_write_line("[gdt64] load data segments");
    __asm__ volatile(
        "mov %0, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%ss\n\t"
        :
        : "rm"(data_selector)
        : "rax", "memory"
    );
    x64_serial_write_line("[gdt64] clear fs/gs");
    __asm__ volatile("xor %%eax, %%eax\n\tmov %%ax, %%fs\n\tmov %%ax, %%gs" : : : "rax", "memory");
    x64_serial_write_line("[gdt64] ltr");
    __asm__ volatile("ltr %0" : : "rm"(tss_selector) : "memory");
    x64_serial_write_line("[gdt64] done");
}

void x64_gdt_set_kernel_stack(uint64_t stack_top) {
    tss.rsp0 = stack_top;
}
