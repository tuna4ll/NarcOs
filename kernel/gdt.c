#include "gdt.h"
#include "string.h"

gdt_entry_t gdt[6];
gdt_ptr_t   gdt_ptr;
tss_entry_t tss_entry;

extern void gdt_flush(uint32_t);
extern void tss_flush();

static void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low    = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high   = (base >> 24) & 0xFF;

    gdt[num].limit_low   = (limit & 0xFFFF);
    gdt[num].granularity = (limit >> 16) & 0x0F;

    gdt[num].granularity |= gran & 0xF0;
    gdt[num].access      = access;
}

static void write_tss(int32_t num, uint16_t ss0, uint32_t esp0) {
    uint32_t base = (uint32_t)&tss_entry;
    uint32_t limit = sizeof(tss_entry_t) - 1;

    gdt_set_gate(num, base, limit, 0x89, 0x00);
    memset(&tss_entry, 0, sizeof(tss_entry_t));

    tss_entry.ss0  = ss0;
    tss_entry.esp0 = esp0;
    tss_entry.iomap_base = sizeof(tss_entry_t); // Point beyond the TSS to indicate no I/O map
}

void init_gdt() {
    gdt_ptr.limit = (sizeof(gdt_entry_t) * 6) - 1;
    gdt_ptr.base  = (uint32_t)&gdt;

    // Default descriptors
    gdt_set_gate(0, 0, 0, 0, 0);                // Null
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); // K-Code
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // K-Data
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF); // U-Code
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); // U-Data
    write_tss(5, 0x10, 0x2800000);              // TSS with kernel stack

    gdt_flush((uint32_t)&gdt_ptr);
    tss_flush();
}

void set_tss_stack(uint32_t stack) {
    tss_entry.esp0 = stack;
}
