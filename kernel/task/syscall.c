#include "syscall.h"
#include <stdint.h>
#include "vbe.h"
#include "memory_alloc.h"
#include "fs.h"
#include "net.h"
#include "paging.h"
#include "process.h"
#include "fd.h"
#include "rtc.h"
#include "usermode.h"
#include "string.h"

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
extern uint8_t __user_region_start[];
extern uint8_t __user_region_end[];

#define SYSCALL_USER_PATH_MAX 256U
#define SYSCALL_USER_TEXT_MAX 1024U

static int copy_argv_from_user(const char* const* user_argv, uint32_t argc,
                               char out_args[PROCESS_MAX_ARGS][PROCESS_MAX_ARG_LEN],
                               const char* out_ptrs[PROCESS_MAX_ARGS]) {
    for (uint32_t i = 0; i < argc; i++) {
        uint32_t user_arg = 0;

        if (copy_from_user(&user_arg, user_argv + i, sizeof(user_arg)) != 0) return -1;
        if (copy_string_from_user(out_args[i], (const char*)user_arg, PROCESS_MAX_ARG_LEN) != 0) return -1;
        out_ptrs[i] = out_args[i];
    }
    return 0;
}

static void kernel_rng_stir() {
    uint32_t mix;

    read_rtc();
    mix = timer_ticks ^ ((uint32_t)get_mouse_x() << 16) ^ (uint32_t)get_mouse_y();
    mix ^= ((uint32_t)get_year() << 24) |
           ((uint32_t)get_month() << 16) |
           ((uint32_t)get_day() << 8) |
           (uint32_t)get_second();
    mix ^= ((uint32_t)get_hour() << 24) | ((uint32_t)get_minute() << 16);
    kernel_rng_state ^= mix + 0x9E3779B9u;
}

static uint32_t kernel_next_random() {
    kernel_rng_stir();
    kernel_rng_state ^= kernel_rng_state << 13;
    kernel_rng_state ^= kernel_rng_state >> 17;
    kernel_rng_state ^= kernel_rng_state << 5;
    if (kernel_rng_state == 0) kernel_rng_state = 0x6D2B79F5u;
    return kernel_rng_state;
}

static int kernel_fill_random(void* buffer, uint32_t length) {
    uint8_t* out = (uint8_t*)buffer;
    uint32_t word = 0;
    uint32_t word_bytes = 0;

    if (!out && length != 0U) return -1;

    while (length != 0U) {
        if (word_bytes == 0U) {
            word = kernel_next_random();
            word_bytes = sizeof(word);
        }
        *out++ = (uint8_t)(word & 0xFFU);
        word >>= 8;
        word_bytes--;
        length--;
    }
    return 0;
}

static int user_range_in_window(uint32_t addr, uint32_t len, uint32_t base, uint32_t size) {
    uint32_t end;
    uint32_t window_end;

    if (len == 0U) return 1;
    if (addr < base) return 0;
    end = addr + len;
    window_end = base + size;
    if (end < addr) return 0;
    return end <= window_end;
}

static int user_range_readable(const void* user_ptr, uint32_t len) {
    uint32_t addr;
    uint32_t code_base = (uint32_t)__user_region_start;
    uint32_t code_size = (uint32_t)(__user_region_end - __user_region_start);

    if (len == 0U) return 1;
    if (!user_ptr) return 0;
    addr = (uint32_t)user_ptr;
    if (user_range_in_window(addr, len, code_base, code_size)) return 1;
    if (user_range_in_window(addr, len, USER_DATA_WINDOW_BASE, USER_DATA_WINDOW_SIZE)) return 1;
    return 0;
}

static int user_range_writable(const void* user_ptr, uint32_t len) {
    if (len == 0U) return 1;
    if (!user_ptr) return 0;
    return user_range_in_window((uint32_t)user_ptr, len, USER_DATA_WINDOW_BASE, USER_DATA_WINDOW_SIZE);
}

int copy_from_user(void* dst, const void* user_src, uint32_t len) {
    if (len == 0U) return 0;
    if (!dst || !user_range_readable(user_src, len)) return -1;
    memcpy(dst, user_src, (size_t)len);
    return 0;
}

int copy_to_user(void* user_dst, const void* src, uint32_t len) {
    if (len == 0U) return 0;
    if (!src || !user_range_writable(user_dst, len)) return -1;
    memcpy(user_dst, src, (size_t)len);
    return 0;
}

int copy_string_from_user(char* dst, const char* user_src, size_t dst_size) {
    size_t i;

    if (!dst || !user_src || dst_size == 0U) return -1;
    for (i = 0; i < dst_size; i++) {
        if (!user_range_readable(user_src + i, 1U)) return -1;
        dst[i] = user_src[i];
        if (dst[i] == '\0') return 0;
    }
    dst[dst_size - 1U] = '\0';
    return -1;
}

static void reactivate_current_user_space(void) {
    process_t* current = process_current();

    if (!current || current->kind != PROCESS_KIND_USER || !current->user_space.valid) return;
    (void)exec_activate_address_space(&current->user_space);
}

void syscall_handler(trap_frame_t* frame) {
    uint32_t eax = frame->eax;
    uint32_t ebx = frame->ebx;
    uint32_t ecx = frame->ecx;
    uint32_t edx = frame->edx;
    uint32_t esi = frame->esi;

    if (eax == SYS_PRINT) {
        char text[SYSCALL_USER_TEXT_MAX];
        if (copy_string_from_user(text, (const char*)ebx, sizeof(text)) != 0) frame->eax = (uint32_t)-1;
        else {
            vga_println(text);
            frame->eax = 0;
        }
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
        frame->eax = (uint32_t)process_current_pid();
    } else if (eax == SYS_GETPPID) {
        frame->eax = (uint32_t)process_current_ppid();
    } else if (eax == SYS_CHDIR) {
        char path[SYSCALL_USER_PATH_MAX];
        frame->eax = copy_string_from_user(path, (const char*)ebx, sizeof(path)) == 0
                         ? (uint32_t)fs_change_dir(path)
                         : (uint32_t)-1;
    } else if (eax == SYS_FS_READ) {
        char path[SYSCALL_USER_PATH_MAX];
        char* buffer;

        if (copy_string_from_user(path, (const char*)ebx, sizeof(path)) != 0 || edx == 0U) {
            frame->eax = (uint32_t)-1;
        } else {
            buffer = (char*)malloc((size_t)edx);
            if (!buffer) frame->eax = (uint32_t)-1;
            else {
                memset(buffer, 0, (size_t)edx);
                frame->eax = (uint32_t)fs_read_file(path, buffer, (size_t)edx);
                if ((int)frame->eax == 0 && copy_to_user((void*)ecx, buffer, edx) != 0) frame->eax = (uint32_t)-1;
                free(buffer);
            }
        }
    } else if (eax == SYS_FS_WRITE) {
        char path[SYSCALL_USER_PATH_MAX];
        char* contents;

        if (copy_string_from_user(path, (const char*)ebx, sizeof(path)) != 0) {
            frame->eax = (uint32_t)-1;
        } else {
            contents = (char*)malloc(MAX_FILE_SIZE + 1U);
            if (!contents) frame->eax = (uint32_t)-1;
            else {
                frame->eax = copy_string_from_user(contents, (const char*)ecx, MAX_FILE_SIZE + 1U) == 0
                                 ? (uint32_t)fs_write_file(path, contents)
                                 : (uint32_t)-1;
                free(contents);
            }
        }
    } else if (eax == SYS_FS_READ_RAW) {
        char path[SYSCALL_USER_PATH_MAX];
        void* buffer;

        if (copy_string_from_user(path, (const char*)ebx, sizeof(path)) != 0 || esi == 0U) {
            frame->eax = (uint32_t)-1;
        } else {
            buffer = malloc((size_t)esi);
            if (!buffer) frame->eax = (uint32_t)-1;
            else {
                frame->eax = (uint32_t)fs_read_file_raw(path, buffer, (size_t)edx, (size_t)esi);
                if ((int)frame->eax > 0 && copy_to_user((void*)ecx, buffer, frame->eax) != 0) frame->eax = (uint32_t)-1;
                free(buffer);
            }
        }
    } else if (eax == SYS_FS_WRITE_RAW) {
        char path[SYSCALL_USER_PATH_MAX];
        void* buffer;

        if (copy_string_from_user(path, (const char*)ebx, sizeof(path)) != 0) {
            frame->eax = (uint32_t)-1;
        } else if (edx == 0U) {
            frame->eax = (uint32_t)fs_write_file_raw(path, 0, 0U);
        } else {
            buffer = malloc((size_t)edx);
            if (!buffer) frame->eax = (uint32_t)-1;
            else {
                frame->eax = copy_from_user(buffer, (const void*)ecx, edx) == 0
                                 ? (uint32_t)fs_write_file_raw(path, buffer, (size_t)edx)
                                 : (uint32_t)-1;
                free(buffer);
            }
        }
    } else if (eax == SYS_FS_LIST) {
        int max_entries = (int)ecx;
        disk_fs_node_t* entries;

        if (max_entries <= 0 || max_entries > MAX_FILES) frame->eax = (uint32_t)-1;
        else {
            entries = (disk_fs_node_t*)malloc((size_t)max_entries * sizeof(disk_fs_node_t));
            if (!entries) frame->eax = (uint32_t)-1;
            else {
                frame->eax = (uint32_t)fs_list_dir_entries(entries, max_entries);
                if ((int)frame->eax > 0 &&
                    copy_to_user((void*)ebx, entries, (uint32_t)frame->eax * (uint32_t)sizeof(disk_fs_node_t)) != 0) {
                    frame->eax = (uint32_t)-1;
                }
                free(entries);
            }
        }
    } else if (eax == SYS_FS_GET_CWD) {
        char* path;

        if (ecx == 0U) frame->eax = (uint32_t)-1;
        else {
            path = (char*)malloc((size_t)ecx);
            if (!path) frame->eax = (uint32_t)-1;
            else {
                memset(path, 0, (size_t)ecx);
                fs_get_current_path(path, (size_t)ecx);
                frame->eax = copy_to_user((void*)ebx, path, ecx) == 0 ? 0U : (uint32_t)-1;
                free(path);
            }
        }
    } else if (eax == SYS_FS_TOUCH) {
        char path[SYSCALL_USER_PATH_MAX];
        int status;

        if (copy_string_from_user(path, (const char*)ebx, sizeof(path)) != 0) {
            frame->eax = (uint32_t)-1;
        } else {
            status = fs_create_file(path);
            if (status != 0 && fs_find_node(path) >= 0) status = 0;
            frame->eax = (uint32_t)status;
        }
    } else if (eax == SYS_FS_MKDIR) {
        char path[SYSCALL_USER_PATH_MAX];
        frame->eax = copy_string_from_user(path, (const char*)ebx, sizeof(path)) == 0
                         ? (uint32_t)fs_create_dir(path)
                         : (uint32_t)-1;
    } else if (eax == SYS_FS_DELETE) {
        char path[SYSCALL_USER_PATH_MAX];
        frame->eax = copy_string_from_user(path, (const char*)ebx, sizeof(path)) == 0
                         ? (uint32_t)fs_delete_file(path)
                         : (uint32_t)-1;
    } else if (eax == SYS_FS_MOVE) {
        char src[SYSCALL_USER_PATH_MAX];
        char dst[SYSCALL_USER_PATH_MAX];

        if (copy_string_from_user(src, (const char*)ebx, sizeof(src)) != 0 ||
            copy_string_from_user(dst, (const char*)ecx, sizeof(dst)) != 0) {
            frame->eax = (uint32_t)-1;
        } else {
            frame->eax = (uint32_t)fs_move_file(src, dst);
        }
    } else if (eax == SYS_FS_RENAME) {
        char path[SYSCALL_USER_PATH_MAX];
        char new_name[SYSCALL_USER_PATH_MAX];

        if (copy_string_from_user(path, (const char*)ebx, sizeof(path)) != 0 ||
            copy_string_from_user(new_name, (const char*)ecx, sizeof(new_name)) != 0) {
            frame->eax = (uint32_t)-1;
        } else {
            frame->eax = (uint32_t)fs_rename(path, new_name);
        }
    } else if (eax == SYS_FS_FIND_NODE) {
        char path[SYSCALL_USER_PATH_MAX];
        frame->eax = copy_string_from_user(path, (const char*)ebx, sizeof(path)) == 0
                         ? (uint32_t)fs_find_node(path)
                         : (uint32_t)-1;
    } else if (eax == SYS_FS_GET_NODE_INFO) {
        disk_fs_node_t node;

        frame->eax = (uint32_t)fs_get_node_info((int)ebx, &node);
        if ((int)frame->eax == 0 && copy_to_user((void*)ecx, &node, sizeof(node)) != 0) frame->eax = (uint32_t)-1;
    } else if (eax == SYS_FS_GET_PATH) {
        char* path;

        if (edx == 0U) frame->eax = (uint32_t)-1;
        else {
            path = (char*)malloc((size_t)edx);
            if (!path) frame->eax = (uint32_t)-1;
            else {
                memset(path, 0, (size_t)edx);
                fs_get_path_by_index((int)ebx, path, (size_t)edx);
                frame->eax = copy_to_user((void*)ecx, path, edx) == 0 ? 0U : (uint32_t)-1;
                free(path);
            }
        }
    } else if (eax == SYS_SNAKE_GET_INPUT) {
        frame->eax = (uint32_t)consume_user_snake_input();
    } else if (eax == SYS_SNAKE_CLOSE) {
        stop_user_snake();
        frame->eax = 0;
    } else if (eax == SYS_RANDOM) {
        frame->eax = kernel_next_random();
    } else if (eax == SYS_GETRANDOM) {
        void* buffer;

        if (ecx == 0U) {
            frame->eax = 0;
        } else {
            buffer = malloc((size_t)ecx);
            if (!buffer) frame->eax = (uint32_t)-1;
            else {
                frame->eax = (uint32_t)kernel_fill_random(buffer, ecx);
                if ((int)frame->eax == 0 && copy_to_user((void*)ebx, buffer, ecx) != 0) frame->eax = (uint32_t)-1;
                free(buffer);
            }
        }
    } else if (eax == SYS_NET_GET_CONFIG) {
        net_ipv4_config_t config;

        frame->eax = (uint32_t)net_get_ipv4_config(&config);
        if ((int)frame->eax == 0 && copy_to_user((void*)ebx, &config, sizeof(config)) != 0) frame->eax = (uint32_t)-1;
    } else if (eax == SYS_NET_DHCP) {
        frame->eax = (uint32_t)net_run_dhcp(0);
    } else if (eax == SYS_NET_RESOLVE) {
        char host[SYSCALL_USER_PATH_MAX];
        uint32_t ip = 0;

        if (copy_string_from_user(host, (const char*)ebx, sizeof(host)) != 0) {
            frame->eax = (uint32_t)-1;
        } else {
            frame->eax = (uint32_t)net_resolve_ipv4(host, &ip);
            if ((int)frame->eax == 0 && copy_to_user((void*)ecx, &ip, sizeof(ip)) != 0) frame->eax = (uint32_t)-1;
        }
    } else if (eax == SYS_NET_NTP_QUERY) {
        char host[SYSCALL_USER_PATH_MAX];
        uint32_t unix_seconds = 0;

        if (copy_string_from_user(host, (const char*)ebx, sizeof(host)) != 0) {
            frame->eax = (uint32_t)-1;
        } else {
            frame->eax = (uint32_t)net_ntp_query(host, &unix_seconds);
            if ((int)frame->eax == 0 && copy_to_user((void*)ecx, &unix_seconds, sizeof(unix_seconds)) != 0) {
                frame->eax = (uint32_t)-1;
            }
        }
    } else if (eax == SYS_NET_PING) {
        char host[SYSCALL_USER_PATH_MAX];
        net_ping_result_t result;

        if (copy_string_from_user(host, (const char*)ebx, sizeof(host)) != 0) {
            frame->eax = (uint32_t)-1;
        } else {
            frame->eax = (uint32_t)net_ping_host(host, &result);
            if ((int)frame->eax == 0 && copy_to_user((void*)ecx, &result, sizeof(result)) != 0) frame->eax = (uint32_t)-1;
        }
    } else if (eax == SYS_NET_SOCKET_OPEN) {
        frame->eax = (uint32_t)net_socket_open((int)ebx);
    } else if (eax == SYS_NET_SOCKET_CONNECT) {
        frame->eax = (uint32_t)net_socket_connect((int)ebx, ecx, (uint16_t)edx, esi);
    } else if (eax == SYS_NET_SOCKET_SEND) {
        void* data;

        if (edx == 0U) {
            frame->eax = (uint32_t)net_socket_send((int)ebx, 0, 0U);
        } else {
            data = malloc((size_t)edx);
            if (!data) frame->eax = (uint32_t)-1;
            else {
                frame->eax = copy_from_user(data, (const void*)ecx, edx) == 0
                                 ? (uint32_t)net_socket_send((int)ebx, data, (uint16_t)edx)
                                 : (uint32_t)-1;
                free(data);
            }
        }
    } else if (eax == SYS_NET_SOCKET_RECV) {
        void* data;

        if (edx == 0U) {
            frame->eax = 0;
        } else {
            data = malloc((size_t)edx);
            if (!data) frame->eax = (uint32_t)-1;
            else {
                frame->eax = (uint32_t)net_socket_recv((int)ebx, data, (uint16_t)edx);
                if ((int)frame->eax > 0 && copy_to_user((void*)ecx, data, frame->eax) != 0) frame->eax = (uint32_t)-1;
                free(data);
            }
        }
    } else if (eax == SYS_NET_SOCKET_AVAILABLE) {
        frame->eax = (uint32_t)net_socket_available((int)ebx);
    } else if (eax == SYS_NET_SOCKET_CLOSE) {
        frame->eax = (uint32_t)net_socket_close((int)ebx);
    } else if (eax == SYS_CLEAR_SCREEN) {
        clear_screen();
        frame->eax = 0;
    } else if (eax == SYS_RTC_GET_LOCAL) {
        rtc_local_time_t out_time;
        read_rtc();
        out_time.year = (uint16_t)get_year();
        out_time.month = get_month();
        out_time.day = get_day();
        out_time.hour = get_hour();
        out_time.minute = get_minute();
        out_time.second = get_second();
        frame->eax = copy_to_user((void*)ebx, &out_time, sizeof(out_time)) == 0 ? 0U : (uint32_t)-1;
    } else if (eax == SYS_RTC_GET_TZ_OFFSET) {
        frame->eax = (uint32_t)rtc_get_timezone_offset_minutes();
    } else if (eax == SYS_RTC_SET_TZ_OFFSET) {
        rtc_set_timezone_offset_minutes((int)ebx);
        frame->eax = 0;
    } else if (eax == SYS_RTC_SAVE_TZ) {
        frame->eax = (uint32_t)rtc_save_timezone_setting();
    } else if (eax == SYS_PRIV_CMD) {
        char arg[SYSCALL_USER_PATH_MAX];

        if (ecx == 0U) {
            frame->eax = (uint32_t)kernel_run_privileged_command((int)ebx, 0);
        } else if (copy_string_from_user(arg, (const char*)ecx, sizeof(arg)) != 0) {
            frame->eax = (uint32_t)-1;
        } else {
            frame->eax = (uint32_t)kernel_run_privileged_command((int)ebx, arg);
        }
    } else if (eax == SYS_PRINT_RAW) {
        char text[SYSCALL_USER_TEXT_MAX];
        if (copy_string_from_user(text, (const char*)ebx, sizeof(text)) != 0) frame->eax = (uint32_t)-1;
        else {
            vga_print(text);
            frame->eax = 0;
        }
    } else if (eax == SYS_GUI_OPEN_NARCPAD_FILE) {
        char path[SYSCALL_USER_PATH_MAX];
        frame->eax = copy_string_from_user(path, (const char*)ebx, sizeof(path)) == 0
                         ? (uint32_t)kernel_gui_open_narcpad_file(path)
                         : (uint32_t)-1;
    } else if (eax == SYS_SPAWN) {
        char path[SYSCALL_USER_PATH_MAX];
        char argv_copy[PROCESS_MAX_ARGS][PROCESS_MAX_ARG_LEN];
        const char* argv_ptrs[PROCESS_MAX_ARGS];

        if (edx > PROCESS_MAX_ARGS || copy_string_from_user(path, (const char*)ebx, sizeof(path)) != 0) {
            frame->eax = (uint32_t)-1;
        } else if (edx != 0U && copy_argv_from_user((const char* const*)ecx, edx, argv_copy, argv_ptrs) != 0) {
            frame->eax = (uint32_t)-1;
        } else {
            frame->eax = (uint32_t)process_create_user(path, edx != 0U ? argv_ptrs : 0, (int)edx, 0U);
        }
    } else if (eax == SYS_EXEC) {
        char path[SYSCALL_USER_PATH_MAX];
        char argv_copy[PROCESS_MAX_ARGS][PROCESS_MAX_ARG_LEN];
        const char* argv_ptrs[PROCESS_MAX_ARGS];

        if (edx > PROCESS_MAX_ARGS || copy_string_from_user(path, (const char*)ebx, sizeof(path)) != 0) {
            frame->eax = (uint32_t)-1;
        } else if (edx != 0U && copy_argv_from_user((const char* const*)ecx, edx, argv_copy, argv_ptrs) != 0) {
            frame->eax = (uint32_t)-1;
        } else if (process_request_exec_current(path, edx != 0U ? argv_ptrs : 0, (int)edx) != 0) {
            frame->eax = (uint32_t)-1;
        } else {
            user_kernel_return_mode = USER_KERNEL_RETURN_KERNEL;
            frame->eax = 0;
        }
    } else if (eax == SYS_WAITPID) {
        process_t* current = process_current();
        int child_status = 0;

        if (current && current->kind == PROCESS_KIND_USER) {
            if (process_request_wait_current((int)ebx, ecx, edx) != 0) {
                frame->eax = (uint32_t)-1;
            } else {
                user_kernel_return_mode = USER_KERNEL_RETURN_KERNEL;
                frame->eax = 0;
            }
        } else {
            frame->eax = (uint32_t)process_waitpid_sync_current((int)ebx, edx, &child_status);
            if ((int)frame->eax > 0 && ecx != 0U && copy_to_user((void*)ecx, &child_status, sizeof(child_status)) != 0) {
                frame->eax = (uint32_t)-1;
            }
        }
    } else if (eax == SYS_KILL) {
        frame->eax = (uint32_t)process_kill_pid((int)ebx);
    } else if (eax == SYS_SLEEP) {
        if (ebx == 0U) {
            frame->eax = 0U;
        } else if (process_request_sleep_current(ebx) != 0) {
            frame->eax = (uint32_t)-1;
        } else {
            user_kernel_return_mode = USER_KERNEL_RETURN_KERNEL;
            frame->eax = 0U;
        }
    } else if (eax == SYS_READ) {
        process_t* current = process_current();
        void* buffer;

        if (!current) frame->eax = (uint32_t)-1;
        else if (edx == 0U) frame->eax = 0U;
        else {
            buffer = malloc((size_t)edx);
            if (!buffer) frame->eax = (uint32_t)-1;
            else {
                frame->eax = (uint32_t)fd_read(current, (int)ebx, buffer, edx);
                if ((int)frame->eax > 0) {
                    reactivate_current_user_space();
                    if (copy_to_user((void*)ecx, buffer, frame->eax) != 0) frame->eax = (uint32_t)-1;
                }
                free(buffer);
            }
        }
    } else if (eax == SYS_WRITE) {
        process_t* current = process_current();
        void* buffer;

        if (!current) frame->eax = (uint32_t)-1;
        else if (edx == 0U) frame->eax = 0U;
        else {
            buffer = malloc((size_t)edx);
            if (!buffer) frame->eax = (uint32_t)-1;
            else {
                frame->eax = copy_from_user(buffer, (const void*)ecx, edx) == 0
                                 ? (uint32_t)fd_write(current, (int)ebx, buffer, edx)
                                 : (uint32_t)-1;
                free(buffer);
            }
        }
    } else if (eax == SYS_CLOSE) {
        frame->eax = (uint32_t)fd_close(process_current(), (int)ebx);
    } else if (eax == SYS_DUP2) {
        frame->eax = (uint32_t)fd_dup2(process_current(), (int)ebx, (int)ecx);
    } else if (eax == SYS_PIPE) {
        process_t* current = process_current();
        int pair[2];

        if (!current) frame->eax = (uint32_t)-1;
        else if (fd_pipe(current, pair) != 0) frame->eax = (uint32_t)-1;
        else {
            reactivate_current_user_space();
            frame->eax = copy_to_user((void*)ebx, pair, sizeof(pair)) == 0 ? 0U : (uint32_t)-1;
            if ((int)frame->eax != 0) {
                (void)fd_close(current, pair[0]);
                (void)fd_close(current, pair[1]);
            }
        }
    } else if (eax == SYS_PROCESS_SNAPSHOT) {
        process_snapshot_entry_t entries[16];

        if ((int)ecx <= 0 || ecx > (uint32_t)(sizeof(entries) / sizeof(entries[0]))) frame->eax = (uint32_t)-1;
        else {
            frame->eax = (uint32_t)process_snapshot(entries, (int)ecx);
            if ((int)frame->eax > 0) {
                reactivate_current_user_space();
                if (copy_to_user((void*)ebx, entries, (uint32_t)frame->eax * (uint32_t)sizeof(entries[0])) != 0) {
                    frame->eax = (uint32_t)-1;
                }
            }
        }
    } else if (eax == SYS_EXIT) {
        if (usermode_schedule_current_process_exit((int)ebx) == 0) {
            frame->eax = 0;
        } else if (usermode_exit_current_task((int)ebx) == 0) {
            frame->eax = 0;
        } else {
            process_exit_current((int)ebx);
        }
    }

    if (user_kernel_return_mode == USER_KERNEL_RETURN_KERNEL && user_current_task_frame_ptr) {
        *user_current_task_frame_ptr = *frame;
    } else {
        set_tss_stack(usermode_active_trap_stack_top());
    }
}

extern void set_idt_gate(int n, uint32_t handler, uint8_t attributes);
extern void isr_syscall();
extern void isr_user_yield();

void init_syscalls() {
    set_idt_gate(0x80, (uint32_t)isr_syscall, 0xEF);
    set_idt_gate(0x81, (uint32_t)isr_user_yield, 0xEF);
}
