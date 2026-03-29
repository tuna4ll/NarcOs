#include "syscall.h"
#include <stdint.h>
#include "vbe.h"
#include "memory_alloc.h"
#include "fs.h"
#include "net.h"
#include "rtc.h"
#include "usermode.h"

extern void vga_println(const char* str);
extern void vga_print(const char* str);
extern void vga_print_color(const char* str, uint8_t color);
extern void vbe_compose_scene_basic();
extern int get_mouse_x();
extern int get_mouse_y();
extern void clear_screen(void);
extern int kernel_run_privileged_command(int cmd, const char* arg);
extern int kernel_gui_open_narcpad_file(const char* path);
static uint32_t last_gui_tick = 0;
extern uint32_t timer_ticks;
static uint32_t kernel_rng_state = 0xA341316Cu;

static uint32_t kernel_next_random() {
    uint32_t mix = timer_ticks ^ ((uint32_t)get_mouse_x() << 16) ^ (uint32_t)get_mouse_y();
    kernel_rng_state ^= mix + 0x9E3779B9u;
    kernel_rng_state ^= kernel_rng_state << 13;
    kernel_rng_state ^= kernel_rng_state >> 17;
    kernel_rng_state ^= kernel_rng_state << 5;
    if (kernel_rng_state == 0) kernel_rng_state = 0x6D2B79F5u;
    return kernel_rng_state;
}

void syscall_handler(trap_frame_t* frame) {
    uint32_t eax = frame->eax;
    uint32_t ebx = frame->ebx;
    uint32_t ecx = frame->ecx;
    uint32_t edx = frame->edx;
    uint32_t esi = frame->esi;

    if (eax == SYS_PRINT) {
        vga_println((const char*)ebx);
    } else if (eax == SYS_MALLOC) {
        frame->eax = (uint32_t)malloc((size_t)ebx);
    } else if (eax == SYS_FREE) {
        free((void*)ebx);
    } else if (eax == SYS_GUI_UPDATE) {
        if (timer_ticks - last_gui_tick > 2) { 
            vbe_compose_scene_basic();
            vbe_update();
            last_gui_tick = timer_ticks;
        }
        
        volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
        static int hb = 0;
        vga[78] = (hb++ % 2) ? 0x2F21 : 0x2F20; // '!' blinking in green
    } else if (eax == SYS_YIELD) {
        frame->eax = 0;
    } else if (eax == SYS_UPTIME) {
        frame->eax = timer_ticks;
    } else if (eax == SYS_GETPID) {
        frame->eax = 1;
    } else if (eax == SYS_CHDIR) {
        frame->eax = (uint32_t)fs_change_dir((const char*)ebx);
    } else if (eax == SYS_FS_READ) {
        frame->eax = (uint32_t)fs_read_file((const char*)ebx, (char*)ecx, (size_t)edx);
    } else if (eax == SYS_FS_WRITE) {
        frame->eax = (uint32_t)fs_write_file((const char*)ebx, (const char*)ecx);
    } else if (eax == SYS_FS_LIST) {
        frame->eax = (uint32_t)fs_list_dir_entries((disk_fs_node_t*)ebx, (int)ecx);
    } else if (eax == SYS_FS_GET_CWD) {
        fs_get_current_path((char*)ebx, (size_t)ecx);
        frame->eax = 0;
    } else if (eax == SYS_FS_TOUCH) {
        int status = fs_create_file((const char*)ebx);
        if (status != 0 && fs_find_node((const char*)ebx) >= 0) status = 0;
        frame->eax = (uint32_t)status;
    } else if (eax == SYS_FS_MKDIR) {
        frame->eax = (uint32_t)fs_create_dir((const char*)ebx);
    } else if (eax == SYS_FS_DELETE) {
        frame->eax = (uint32_t)fs_delete_file((const char*)ebx);
    } else if (eax == SYS_FS_MOVE) {
        frame->eax = (uint32_t)fs_move_file((const char*)ebx, (const char*)ecx);
    } else if (eax == SYS_FS_RENAME) {
        frame->eax = (uint32_t)fs_rename((const char*)ebx, (const char*)ecx);
    } else if (eax == SYS_FS_FIND_NODE) {
        frame->eax = (uint32_t)fs_find_node((const char*)ebx);
    } else if (eax == SYS_FS_GET_NODE_INFO) {
        frame->eax = (uint32_t)fs_get_node_info((int)ebx, (disk_fs_node_t*)ecx);
    } else if (eax == SYS_FS_GET_PATH) {
        fs_get_path_by_index((int)ebx, (char*)ecx, (size_t)edx);
        frame->eax = 0;
    } else if (eax == SYS_SNAKE_GET_INPUT) {
        frame->eax = (uint32_t)consume_user_snake_input();
    } else if (eax == SYS_SNAKE_CLOSE) {
        stop_user_snake();
        frame->eax = 0;
    } else if (eax == SYS_RANDOM) {
        frame->eax = kernel_next_random();
    } else if (eax == SYS_NET_GET_CONFIG) {
        frame->eax = (uint32_t)net_get_ipv4_config((net_ipv4_config_t*)ebx);
    } else if (eax == SYS_NET_DHCP) {
        frame->eax = (uint32_t)net_run_dhcp(0);
    } else if (eax == SYS_NET_RESOLVE) {
        frame->eax = (uint32_t)net_resolve_ipv4((const char*)ebx, (uint32_t*)ecx);
    } else if (eax == SYS_NET_NTP_QUERY) {
        frame->eax = (uint32_t)net_ntp_query((const char*)ebx, (uint32_t*)ecx);
    } else if (eax == SYS_NET_PING) {
        frame->eax = (uint32_t)net_ping_host((const char*)ebx, (net_ping_result_t*)ecx);
    } else if (eax == SYS_NET_SOCKET_OPEN) {
        frame->eax = (uint32_t)net_socket_open((int)ebx);
    } else if (eax == SYS_NET_SOCKET_CONNECT) {
        frame->eax = (uint32_t)net_socket_connect((int)ebx, ecx, (uint16_t)edx, esi);
    } else if (eax == SYS_NET_SOCKET_SEND) {
        frame->eax = (uint32_t)net_socket_send((int)ebx, (const void*)ecx, (uint16_t)edx);
    } else if (eax == SYS_NET_SOCKET_RECV) {
        frame->eax = (uint32_t)net_socket_recv((int)ebx, (void*)ecx, (uint16_t)edx);
    } else if (eax == SYS_NET_SOCKET_AVAILABLE) {
        frame->eax = (uint32_t)net_socket_available((int)ebx);
    } else if (eax == SYS_NET_SOCKET_CLOSE) {
        frame->eax = (uint32_t)net_socket_close((int)ebx);
    } else if (eax == SYS_CLEAR_SCREEN) {
        clear_screen();
        frame->eax = 0;
    } else if (eax == SYS_RTC_GET_LOCAL) {
        rtc_local_time_t* out_time = (rtc_local_time_t*)ebx;
        read_rtc();
        out_time->year = (uint16_t)get_year();
        out_time->month = get_month();
        out_time->day = get_day();
        out_time->hour = get_hour();
        out_time->minute = get_minute();
        out_time->second = get_second();
        frame->eax = 0;
    } else if (eax == SYS_RTC_GET_TZ_OFFSET) {
        frame->eax = (uint32_t)rtc_get_timezone_offset_minutes();
    } else if (eax == SYS_RTC_SET_TZ_OFFSET) {
        rtc_set_timezone_offset_minutes((int)ebx);
        frame->eax = 0;
    } else if (eax == SYS_RTC_SAVE_TZ) {
        frame->eax = (uint32_t)rtc_save_timezone_setting();
    } else if (eax == SYS_PRIV_CMD) {
        frame->eax = (uint32_t)kernel_run_privileged_command((int)ebx, (const char*)ecx);
    } else if (eax == SYS_PRINT_RAW) {
        vga_print((const char*)ebx);
        frame->eax = 0;
    } else if (eax == SYS_GUI_OPEN_NARCPAD_FILE) {
        frame->eax = (uint32_t)kernel_gui_open_narcpad_file((const char*)ebx);
    } else if (eax == SYS_EXIT) {
        stop_user_snake();
        frame->eax = 0;
    }
}

extern void set_idt_gate(int n, uint32_t handler, uint8_t attributes);
extern void isr_syscall();
extern void isr_user_yield();

void init_syscalls() {
    set_idt_gate(0x80, (uint32_t)isr_syscall, 0xEF);
    set_idt_gate(0x81, (uint32_t)isr_user_yield, 0xEF);
}
