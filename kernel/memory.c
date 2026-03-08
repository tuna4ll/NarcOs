// kernel/memory.c

#include <stdint.h>

extern void vga_print(const char* str);
extern void vga_print_color(const char* str, uint8_t color);
extern void vga_print_int(int num);
extern void vga_println(const char* str);

#define COLOR_WARN 0x0E
#define COLOR_CYAN 0x0B


#define E820_COUNT_PTR ((uint16_t*)0x5000)
#define E820_DATA_PTR  ((uint8_t*)0x5002)

typedef struct {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t ext_attr;
} __attribute__((packed)) e820_entry_t;

void print_memory_info();

void print_memory_info() {
    uint16_t entry_count = *E820_COUNT_PTR;
    
    vga_print_color("[INFO] Memory Map (E820):\n", COLOR_WARN);
    
    if (entry_count == 0) {
        vga_println("Memory map not found (Invalid BIOS?).");
        return;
    }
    
    e820_entry_t* entries = (e820_entry_t*)E820_DATA_PTR;
    uint64_t total_usable_ram = 0;
    
    for (int i = 0; i < entry_count; i++) {
        vga_print("Region ");
        vga_print_int(i);
        vga_print(": Base 0x");

        
        uint32_t len_mb = (uint32_t)(entries[i].length / (1024 * 1024));
        uint32_t len_kb = (uint32_t)(entries[i].length / 1024);
        
        if (len_mb > 0) {
            vga_print_int((int)len_mb);
            vga_print(" MB");
        } else {
            vga_print_int((int)len_kb);
            vga_print(" KB");
        }
        
        vga_print(" - Type: ");
        if (entries[i].type == 1) {
            vga_print_color("Usable (RAM)\n", COLOR_CYAN);
            total_usable_ram += entries[i].length;
        } else if (entries[i].type == 2) {
            vga_print("Reserved\n");
        } else {
            vga_print("Other (ACPI etc.)\n");
        }
    }
    
    vga_print_color("Total Usable RAM: ", COLOR_WARN);
    vga_print_int((int)(total_usable_ram / (1024 * 1024)));
    vga_println(" MB");
}
