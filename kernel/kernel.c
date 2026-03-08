
#include <stdint.h>
#include "string.h"
#include "fs.h"
#include "rtc.h"
#include "editor.h"
#include "memory_alloc.h"

extern void outb(uint16_t port, uint8_t val);
// (screen.c)
extern void clear_screen();
extern void vga_print(const char* str);
extern void vga_print_color(const char* str, uint8_t color);
extern void vga_println(const char* str);
extern void vga_print_int(int num);

// (keyboard.c)
extern void init_keyboard();

// (memory.c)
extern void print_memory_info();

// (snake.c)
extern void snake_main();
extern int snake_running;

// --- IDT and Interrupt Subsystem ---

typedef struct {
    uint16_t isr_low;      // ISR Address Low 16-bit
    uint16_t kernel_cs;    // Kernel Code Segment (GDT Selector)
    uint8_t  reserved;     // 0
    uint8_t  attributes;   // Type and Attributes
    uint16_t isr_high;     // ISR Address High 16-bit
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idtr_t;

idt_entry_t idt[256];
idtr_t idtr;

volatile uint32_t timer_ticks = 0;

extern void isr_default();
extern void irq0_timer();
extern void irq1_keyboard();

void set_idt_gate(int n, uint32_t handler) {
    idt[n].isr_low = (uint16_t)(handler & 0xFFFF);
    idt[n].kernel_cs = 0x08; // GDT'deki kod segment selector (bknz stage2.asm)
    idt[n].reserved = 0;
    idt[n].attributes = 0x8E; // Interrupt Gate, Ring 0, Present
    idt[n].isr_high = (uint16_t)((handler >> 16) & 0xFFFF);
}

void init_pic() {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20); // Master PIC Vector Offset (0x20 = 32)
    outb(0xA1, 0x28); // Slave PIC Vector Offset (0x28 = 40)
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    outb(0x21, 0xFC); 
    outb(0xA1, 0xFF); 
}

void init_pit() {
    uint32_t freq = 100; 
    uint32_t divisor = 1193180 / freq;
    outb(0x43, 0x36); 
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

void load_idt() {
    idtr.base = (uint32_t)&idt;
    idtr.limit = 256 * sizeof(idt_entry_t) - 1;

    for (int i = 0; i < 256; i++) {
        set_idt_gate(i, (uint32_t)isr_default);
    }

    set_idt_gate(32, (uint32_t)irq0_timer);
    set_idt_gate(33, (uint32_t)irq1_keyboard);

    asm volatile("lidt %0" : : "m"(idtr));
    asm volatile("sti"); // Interruptlari Ac
}

// Timer C Handler
void handle_timer() {
    timer_ticks++;
    // EOI (End of Interrupt) 
    outb(0x20, 0x20);
}

void isr_handler_default() {
    outb(0x20, 0x20); // Master EOI
    outb(0xA0, 0x20); // Slave EOI
}


void execute_command(char* cmd) {
    if (cmd[0] == '\0') return;

    char arg1[32] = {0};
    char arg2[128] = {0};
    int i = 0, j = 0;
    
    while (cmd[i] != ' ' && cmd[i] != '\0' && i < 31) {
        arg1[i] = cmd[i];
        i++;
    }
    arg1[i] = '\0';
    
    while (cmd[i] == ' ') i++;
    
    while (cmd[i] != '\0' && j < 127) {
        arg2[j] = cmd[i];
        i++; j++;
    }
    arg2[j] = '\0';


    if (strcmp(arg1, "help") == 0) {
        vga_println("NarcOs Shell");
        vga_println("  help   - Show this menu");
        vga_println("  clear  - Clear the screen");
        vga_println("  mem    - Memory map");
        vga_println("  snake  - Snake game");
        vga_println("  ver    - Show version");
        vga_println("  uptime - Show system uptime in seconds");
        vga_println("  date   - Show current date (RTC)");
        vga_println("  time   - Show current time (RTC)");
        vga_println("  ls     - List files");
        vga_println("  cat    - Read file (cat <file>)");
        vga_println("  write  - Write to file (write <file> <text>)");
        vga_println("  edit   - Open file in NarcVim (edit <file>)");
        vga_println("  mkdir  - Create directory (mkdir <name>)");
        vga_println("  cd     - Change directory (cd <name> or cd ..)");
        vga_println("  rm     - Delete file (rm <file>)");
        vga_println("  malloc_test - Test dynamic heap memory");
    } else if (strcmp(arg1, "clear") == 0) {
        clear_screen();
    } else if (strcmp(arg1, "mem") == 0) {
        print_memory_info();
    } else if (strcmp(arg1, "malloc_test") == 0) {
        malloc_test();
    } else if (strcmp(arg1, "ver") == 0) {
        vga_println("NarcOs");
    } else if (strcmp(arg1, "uptime") == 0) {
        vga_print("System Uptime (seconds): ");
        vga_print_int(timer_ticks / 100);
        vga_println("");
    } else if (strcmp(arg1, "date") == 0) {
        read_rtc();
        vga_print("Current Date: 20");
        vga_print_int(get_year());
        vga_print("-");
        if (get_month() < 10) vga_print("0");
        vga_print_int(get_month());
        vga_print("-");
        if (get_day() < 10) vga_print("0");
        vga_print_int(get_day());
        vga_println("");
    } else if (strcmp(arg1, "time") == 0) {
        read_rtc();
        vga_print("Current Time: ");
        if (get_hour() < 10) vga_print("0");
        vga_print_int(get_hour());
        vga_print(":");
        if (get_minute() < 10) vga_print("0");
        vga_print_int(get_minute());
        vga_print(":");
        if (get_second() < 10) vga_print("0");
        vga_print_int(get_second());
        vga_println("");
    } else if (strcmp(arg1, "snake") == 0) {
        snake_main();
        clear_screen();
        vga_print_color("Bad score I guess...\n", 0x0C);
    } else if (strcmp(arg1, "ls") == 0) {
        fs_list_dir();
    } else if (strcmp(arg1, "cat") == 0) {
        if (arg2[0] == '\0') {
            vga_print_color("Usage: cat <file>\n", 0x0E);
            return;
        }
        char buffer[2048] = {0};
        if (fs_read_file(arg2, buffer, sizeof(buffer)) == 0) {
            vga_println(buffer);
        } else {
            vga_print_color("error: File not found.\n", 0x0C);
        }
    } else if (strcmp(arg1, "write") == 0) {
        if (arg2[0] == '\0') {
            vga_print_color("Usage: write <file> <text>\n", 0x0E);
            return;
        }
        
        char file_name[32] = {0};
        int k = 0;
        
        while(arg2[k] != ' ' && arg2[k] != '\0' && k < 31) {
            file_name[k] = arg2[k];
            k++;
        }
        file_name[k] = '\0';
        
        while (arg2[k] == ' ') k++;
        
        char* file_content = &arg2[k];
        
        if (file_content[0] == '\0') {
            vga_print_color("hata: Metin bos olamaz.\n", 0x0C);
            return;
        }
        
        if (fs_write_file(file_name, file_content) == 0) {
            vga_println("Success.");
        } else {
            vga_print_color("error: Failed to create file or not enough space.\n", 0x0C);
        }
    } else if (strcmp(arg1, "edit") == 0) {
        if (arg2[0] == '\0') {
            vga_print_color("Usage: edit <file>\n", 0x0E);
            return;
        }
        editor_start(arg2);
        clear_screen();
        vga_print_color("Exited NarcVim. ", 0x0A);
        vga_println(arg2);
    } else if (strcmp(arg1, "mkdir") == 0) {
        if (arg2[0] == '\0') {
            vga_print_color("Usage: mkdir <name>\n", 0x0E);
            return;
        }
        if (fs_create_dir(arg2) == 0) {
            vga_println("Directory created.");
        } else {
            vga_print_color("error: Failed to create directory.\n", 0x0C);
        }
    } else if (strcmp(arg1, "cd") == 0) {
        if (arg2[0] == '\0') {
            vga_print_color("Usage: cd <name>\n", 0x0E);
            return;
        }
        if (fs_change_dir(arg2) != 0) {
            vga_print_color("error: Directory not found.\n", 0x0C);
        }
    } else if (strcmp(arg1, "rm") == 0) {
        if (arg2[0] == '\0') {
            vga_print_color("Usage: rm <file>\n", 0x0E);
            return;
        }
        if (fs_delete_file(arg2) == 0) {
            vga_println("Success. File deleted.");
        } else {
            vga_print_color("error: File not found.\n", 0x0C);
        }
    } else {
        vga_print_color("Error: Unknown command '", 0x0C);
        vga_print_color(arg1, 0x0C);
        vga_println("'");
    }
}


// Keyboard Variables
extern char cmd_to_execute[128];
extern volatile int cmd_ready;

extern int current_dir_index;


extern void get_current_dir_name(char* buf); // Define a helper in fs.c

void print_prompt() {
    vga_print_color("root@narc:", 0x0A);
    vga_print_color("/", 0x0B);
    
    char dname[32];
    get_current_dir_name(dname);
    if (dname[0] != '\0') {
        vga_print_color(dname, 0x0B);
    }
    vga_print_color("$ ", 0x0A);
}

void kmain() {
    init_pic();
    init_pit();
    load_idt();
    init_keyboard();
    init_fs();
    init_heap();
    clear_screen();

    vga_print_color("\nNarcOs\n", 0x0B);
    vga_print_color("=====================================\n\n", 0x0B);

    print_prompt();

    while (1) {
        if (cmd_ready) {
            execute_command(cmd_to_execute);
            cmd_ready = 0;
            print_prompt();
        }
        else if (!snake_running) {
            asm volatile("hlt");
        }
    }
}
