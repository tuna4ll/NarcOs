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
#include "serial.h"
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

static void syscall_set_result(arch_trap_frame_t* frame, intptr_t value) {
    arch_frame_set_return_value(frame, (uintptr_t)value);
}

static uint8_t current_user_argv_slot_size(void) {
    process_t* current = process_current();
    exec_image_t image;

    /* Static user tasks such as the desktop shell are compiled for the
       kernel's native word size even though they do not own an exec image. */
    if (!current || current->kind != PROCESS_KIND_USER) return sizeof(uintptr_t);
    if (exec_query_image(&current->user_space, &image) != EXEC_OK) return sizeof(uint32_t);
    return image.image_class == EXEC_IMAGE_CLASS_ELF64 ? sizeof(uint64_t) : sizeof(uint32_t);
}

static int copy_argv_from_user(uintptr_t user_argv, uint32_t argc, uint8_t argv_slot_size,
                               char out_args[PROCESS_MAX_ARGS][PROCESS_MAX_ARG_LEN],
                               const char* out_ptrs[PROCESS_MAX_ARGS]) {
    if ((argc != 0U && user_argv == 0U) ||
        (argv_slot_size != sizeof(uint32_t) && argv_slot_size != sizeof(uint64_t))) {
        return -1;
    }

    for (uint32_t i = 0; i < argc; i++) {
        uintptr_t user_arg = 0U;

        if (argv_slot_size == sizeof(uint64_t)) {
            uint64_t raw_arg = 0ULL;

            if (copy_from_user(&raw_arg, (const void*)(user_argv + (uintptr_t)i * sizeof(raw_arg)),
                               sizeof(raw_arg)) != 0) {
                return -1;
            }
            user_arg = (uintptr_t)raw_arg;
        } else {
            uint32_t raw_arg = 0U;

            if (copy_from_user(&raw_arg, (const void*)(user_argv + (uintptr_t)i * sizeof(raw_arg)),
                               sizeof(raw_arg)) != 0) {
                return -1;
            }
            user_arg = (uintptr_t)raw_arg;
        }

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

static int user_range_in_window(uintptr_t addr, size_t len, uintptr_t base, size_t size) {
    uintptr_t end;
    uintptr_t window_end;

    if (len == 0U) return 1;
    if (addr < base) return 0;
    end = addr + len;
    window_end = base + size;
    if (end < addr) return 0;
    return end <= window_end;
}

static int user_range_readable(const void* user_ptr, uint32_t len) {
    uintptr_t addr;
    uintptr_t code_base = (uintptr_t)__user_region_start;
    size_t code_size = (size_t)(__user_region_end - __user_region_start);

    if (len == 0U) return 1;
    if (!user_ptr) return 0;
    addr = (uintptr_t)user_ptr;
    if (user_range_in_window(addr, (size_t)len, code_base, code_size)) return 1;
    if (user_range_in_window(addr, (size_t)len, USER_DATA_WINDOW_BASE, USER_DATA_WINDOW_SIZE)) return 1;
    return 0;
}

static int user_range_writable(const void* user_ptr, uint32_t len) {
    uintptr_t addr;
    uintptr_t code_base = (uintptr_t)__user_region_start;
    size_t code_size = (size_t)(__user_region_end - __user_region_start);

    if (len == 0U) return 1;
    if (!user_ptr) return 0;
    addr = (uintptr_t)user_ptr;
    if (user_range_in_window(addr, (size_t)len, code_base, code_size)) return 1;
    return user_range_in_window(addr, (size_t)len, USER_DATA_WINDOW_BASE, USER_DATA_WINDOW_SIZE);
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

typedef void (*syscall_handler_routine)(arch_trap_frame_t*, arch_syscall_state_t *regs);

void syscall_PRINT(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;

    char text[SYSCALL_USER_TEXT_MAX];

    if (copy_string_from_user(text, (const char*)arg0, sizeof(text)) != 0) syscall_set_result(frame, -1);
    else {
        vga_println(text);
        syscall_set_result(frame, 0);
    }
}

void syscall_MALLOC(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;

    syscall_set_result(frame, (intptr_t)(uintptr_t)malloc((size_t)arg0));
}

void syscall_FREE(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;

    free((void*)arg0);
    syscall_set_result(frame, 0);
}

void syscall_GUI_UPDATE(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    (void)regs;

    if (timer_ticks - last_gui_tick > 2) { 
        vbe_compose_scene_basic();
        vbe_update();
        last_gui_tick = timer_ticks;
    }

    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    static int hb = 0;
    vga[78] = (hb++ % 2) ? 0x2F21 : 0x2F20; // '!' blinking in green
    syscall_set_result(frame, 0);
}

void syscall_YIELD(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    (void)regs;

    syscall_set_result(frame, 0);
}

void syscall_UPTIME(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    (void)regs;

    syscall_set_result(frame, (intptr_t)timer_ticks);
}

void syscall_GETPID(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    (void)regs;

    syscall_set_result(frame, process_current_pid());
}

void syscall_GETPPID(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    (void)regs;

    syscall_set_result(frame, process_current_ppid());
}

void syscall_CHDIR(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;

    char path[SYSCALL_USER_PATH_MAX];

    syscall_set_result(frame, copy_string_from_user(path, (const char*)arg0, sizeof(path)) == 0
                                  ? fs_change_dir(path)
                                  : -1);
}

void syscall_FS_READ(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;
    uintptr_t arg1 = (uintptr_t)regs->arg1;
    uintptr_t arg2 = (uintptr_t)regs->arg2;

    char path[SYSCALL_USER_PATH_MAX];
    char* buffer;
    int status;

    if (copy_string_from_user(path, (const char*)arg0, sizeof(path)) != 0 || arg2 == 0U) {
        syscall_set_result(frame, -1);
    } else {
        buffer = (char*)malloc((size_t)arg2);
        if (!buffer) syscall_set_result(frame, -1);
        else {
            memset(buffer, 0, (size_t)arg2);
            status = fs_read_file(path, buffer, (size_t)arg2);
            if (status == 0 && copy_to_user((void*)arg1, buffer, (uint32_t)arg2) != 0) status = -1;
            syscall_set_result(frame, status);
            free(buffer);
        }
    }
}

void syscall_FS_WRITE(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;
    uintptr_t arg1 = (uintptr_t)regs->arg1;

    char path[SYSCALL_USER_PATH_MAX];
    char* contents;
    int status;

    if (copy_string_from_user(path, (const char*)arg0, sizeof(path)) != 0) {
        syscall_set_result(frame, -1);
    } else {
        contents = (char*)malloc(MAX_TEXT_FILE_SIZE + 1U);
        if (!contents) syscall_set_result(frame, -1);
        else {
            status = copy_string_from_user(contents, (const char*)arg1, MAX_TEXT_FILE_SIZE + 1U) == 0
                         ? fs_write_file(path, contents)
                         : -1;
            syscall_set_result(frame, status);
            free(contents);
        }
    }
}

void syscall_FS_READ_RAW(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;
    uintptr_t arg1 = (uintptr_t)regs->arg1;
    uintptr_t arg2 = (uintptr_t)regs->arg2;
    uintptr_t arg3 = (uintptr_t)regs->arg3;

    char path[SYSCALL_USER_PATH_MAX];
    void* buffer;
    int status;

    if (copy_string_from_user(path, (const char*)arg0, sizeof(path)) != 0 || arg2 == 0U) {
        syscall_set_result(frame, -1);
    } else {
        buffer = malloc((size_t)arg2);
        if (!buffer) syscall_set_result(frame, -1);
        else {
            status = fs_read_file_raw(path, buffer, (size_t)arg3, (size_t)arg2);
            if (status > 0 && copy_to_user((void*)arg1, buffer, (uint32_t)status) != 0) status = -1;
            syscall_set_result(frame, status);
            free(buffer);
        }
    }
}

void syscall_FS_WRITE_RAW(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;
    uintptr_t arg1 = (uintptr_t)regs->arg1;
    uintptr_t arg2 = (uintptr_t)regs->arg2;

    char path[SYSCALL_USER_PATH_MAX];
    void* buffer;
    int status;

    if (copy_string_from_user(path, (const char*)arg0, sizeof(path)) != 0) {
        syscall_set_result(frame, -1);
    } else if (arg2 == 0U) {
        syscall_set_result(frame, fs_write_file_raw(path, 0, 0U));
    } else {
        buffer = malloc((size_t)arg2);
        if (!buffer) syscall_set_result(frame, -1);
        else {
            status = copy_from_user(buffer, (const void*)arg1, (uint32_t)arg2) == 0
                         ? fs_write_file_raw(path, buffer, (size_t)arg2)
                         : -1;
            syscall_set_result(frame, status);
            free(buffer);
        }
    }
}

void syscall_FS_LIST(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;
    uintptr_t arg1 = (uintptr_t)regs->arg1;

    int max_entries = (int)arg1;
    disk_fs_node_t* entries;
    int status;

    if (max_entries <= 0 || max_entries > MAX_FILES) syscall_set_result(frame, -1);
    else {
        entries = (disk_fs_node_t*)malloc((size_t)max_entries * sizeof(disk_fs_node_t));
        if (!entries) syscall_set_result(frame, -1);
        else {
            status = fs_list_dir_entries(entries, max_entries);
            if (status > 0 &&
                copy_to_user((void*)arg0, entries,
                             (uint32_t)status * (uint32_t)sizeof(disk_fs_node_t)) != 0) {
                status = -1;
            }
            syscall_set_result(frame, status);
            free(entries);
        }
    }
}

void syscall_FS_GET_CWD(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;
    uintptr_t arg1 = (uintptr_t)regs->arg1;

    char* path;

    if (arg1 == 0U) syscall_set_result(frame, -1);
    else {
        path = (char*)malloc((size_t)arg1);
        if (!path) syscall_set_result(frame, -1);
        else {
            memset(path, 0, (size_t)arg1);
            fs_get_current_path(path, (size_t)arg1);
            syscall_set_result(frame, copy_to_user((void*)arg0, path, (uint32_t)arg1) == 0 ? 0 : -1);
            free(path);
        }
    }
}

void syscall_FS_TOUCH(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;

    char path[SYSCALL_USER_PATH_MAX];
    int status;

    if (copy_string_from_user(path, (const char*)arg0, sizeof(path)) != 0) {
        syscall_set_result(frame, -1);
    } else {
        status = fs_create_file(path);
        if (status != 0 && fs_find_node(path) >= 0) status = 0;
        syscall_set_result(frame, status);
    }
}

void syscall_FS_MKDIR(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;

    char path[SYSCALL_USER_PATH_MAX];

    syscall_set_result(frame, copy_string_from_user(path, (const char*)arg0, sizeof(path)) == 0
                                  ? fs_create_dir(path)
                                  : -1);
}

void syscall_FS_DELETE(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;

    char path[SYSCALL_USER_PATH_MAX];

    syscall_set_result(frame, copy_string_from_user(path, (const char*)arg0, sizeof(path)) == 0
                                  ? fs_delete_file(path)
                                  : -1);
}

void syscall_FS_MOVE(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;
    uintptr_t arg1 = (uintptr_t)regs->arg1;

    char src[SYSCALL_USER_PATH_MAX];
    char dst[SYSCALL_USER_PATH_MAX];

    if (copy_string_from_user(src, (const char*)arg0, sizeof(src)) != 0 ||
        copy_string_from_user(dst, (const char*)arg1, sizeof(dst)) != 0) {
        syscall_set_result(frame, -1);
    } else {
        syscall_set_result(frame, fs_move_file(src, dst));
    }
}

void syscall_FS_RENAME(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;
    uintptr_t arg1 = (uintptr_t)regs->arg1;

    char path[SYSCALL_USER_PATH_MAX];
    char new_name[SYSCALL_USER_PATH_MAX];

    if (copy_string_from_user(path, (const char*)arg0, sizeof(path)) != 0 ||
        copy_string_from_user(new_name, (const char*)arg1, sizeof(new_name)) != 0) {
        syscall_set_result(frame, -1);
    } else {
        syscall_set_result(frame, fs_rename(path, new_name));
    }
}

void syscall_FS_FIND_NODE(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;

    char path[SYSCALL_USER_PATH_MAX];

    syscall_set_result(frame, copy_string_from_user(path, (const char*)arg0, sizeof(path)) == 0
                                  ? fs_find_node(path)
                                  : -1);
}

void syscall_FS_GET_NODE_INFO(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;
    uintptr_t arg1 = (uintptr_t)regs->arg1;

    disk_fs_node_t node;
    int status;

    status = fs_get_node_info((int)arg0, &node);
    if (status == 0 && copy_to_user((void*)arg1, &node, sizeof(node)) != 0) status = -1;
    syscall_set_result(frame, status);
}

void syscall_FS_GET_PATH(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;
    uintptr_t arg1 = (uintptr_t)regs->arg1;
    uintptr_t arg2 = (uintptr_t)regs->arg2;

    char* path;

    if (arg2 == 0U) syscall_set_result(frame, -1);
    else {
        path = (char*)malloc((size_t)arg2);
        if (!path) syscall_set_result(frame, -1);
        else {
            memset(path, 0, (size_t)arg2);
            fs_get_path_by_index((int)arg0, path, (size_t)arg2);
            syscall_set_result(frame, copy_to_user((void*)arg1, path, (uint32_t)arg2) == 0 ? 0 : -1);
            free(path);
        }
    }
}

void syscall_SNAKE_GET_INPUT(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    (void)regs;

    syscall_set_result(frame, consume_user_snake_input());
}

void syscall_SNAKE_CLOSE(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    (void)regs;

    stop_user_snake();
    syscall_set_result(frame, 0);
}

void syscall_RANDOM(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    (void)regs;

    syscall_set_result(frame, (intptr_t)kernel_next_random());
}

void syscall_GETRANDOM(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;
    uintptr_t arg1 = (uintptr_t)regs->arg1;

    void* buffer;
    int status;

    if (arg1 == 0U) {
        syscall_set_result(frame, 0);
    } else {
        buffer = malloc((size_t)arg1);
        if (!buffer) syscall_set_result(frame, -1);
        else {
            status = kernel_fill_random(buffer, (uint32_t)arg1);
            if (status == 0 && copy_to_user((void*)arg0, buffer, (uint32_t)arg1) != 0) status = -1;
            syscall_set_result(frame, status);
            free(buffer);
        }
    }
}

void syscall_NET_GET_CONFIG(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;

    net_ipv4_config_t config;
    int status;

    status = net_get_ipv4_config(&config);
    if (status == 0 && copy_to_user((void*)arg0, &config, sizeof(config)) != 0) status = -1;
    syscall_set_result(frame, status);
}

void syscall_NET_DHCP(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    (void)regs;

    syscall_set_result(frame, net_run_dhcp(0));
}

void syscall_NET_RESOLVE(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;
    uintptr_t arg1 = (uintptr_t)regs->arg1;

    char host[SYSCALL_USER_PATH_MAX];
    uint32_t ip = 0;
    int status;

    if (copy_string_from_user(host, (const char*)arg0, sizeof(host)) != 0) {
        syscall_set_result(frame, -1);
    } else {
        status = net_resolve_ipv4(host, &ip);
        if (status == 0 && copy_to_user((void*)arg1, &ip, sizeof(ip)) != 0) status = -1;
        syscall_set_result(frame, status);
    }
}

void syscall_NET_NTP_QUERY(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;
    uintptr_t arg1 = (uintptr_t)regs->arg1;

    char host[SYSCALL_USER_PATH_MAX];
    uint32_t unix_seconds = 0;
    int status;

    if (copy_string_from_user(host, (const char*)arg0, sizeof(host)) != 0) {
        syscall_set_result(frame, -1);
    } else {
        status = net_ntp_query(host, &unix_seconds);
        if (status == 0 && copy_to_user((void*)arg1, &unix_seconds, sizeof(unix_seconds)) != 0) status = -1;
        syscall_set_result(frame, status);
    }
}

void syscall_NET_PING(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;
    uintptr_t arg1 = (uintptr_t)regs->arg1;

    char host[SYSCALL_USER_PATH_MAX];
    net_ping_result_t result;
    int status;

    if (copy_string_from_user(host, (const char*)arg0, sizeof(host)) != 0) {
        syscall_set_result(frame, -1);
    } else {
        status = net_ping_host(host, &result);
        if (status == 0 && copy_to_user((void*)arg1, &result, sizeof(result)) != 0) status = -1;
        syscall_set_result(frame, status);
    }
}

void syscall_NET_SOCKET_OPEN(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;

    syscall_set_result(frame, net_socket_open((int)arg0));
}

void syscall_NET_SOCKET_CONNECT(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;
    uintptr_t arg1 = (uintptr_t)regs->arg1;
    uintptr_t arg2 = (uintptr_t)regs->arg2;
    uintptr_t arg3 = (uintptr_t)regs->arg3;

    syscall_set_result(frame, net_socket_connect((int)arg0, (uint32_t)arg1, (uint16_t)arg2, (uint32_t)arg3));
}

void syscall_NET_SOCKET_SEND(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;
    uintptr_t arg1 = (uintptr_t)regs->arg1;
    uintptr_t arg2 = (uintptr_t)regs->arg2;

    void* data;
    int status;

    if (arg2 == 0U) {
        syscall_set_result(frame, net_socket_send((int)arg0, 0, 0U));
    } else {
        data = malloc((size_t)arg2);
        if (!data) syscall_set_result(frame, -1);
        else {
            status = copy_from_user(data, (const void*)arg1, (uint32_t)arg2) == 0
                         ? net_socket_send((int)arg0, data, (uint16_t)arg2)
                         : -1;
            syscall_set_result(frame, status);
            free(data);
        }
    }
}

void syscall_NET_SOCKET_RECV(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;
    uintptr_t arg1 = (uintptr_t)regs->arg1;
    uintptr_t arg2 = (uintptr_t)regs->arg2;

    void* data;
    int status;

    if (arg2 == 0U) {
        syscall_set_result(frame, 0);
    } else {
        data = malloc((size_t)arg2);
        if (!data) syscall_set_result(frame, -1);
        else {
            status = net_socket_recv((int)arg0, data, (uint16_t)arg2);
            if (status > 0 && copy_to_user((void*)arg1, data, (uint32_t)status) != 0) status = -1;
            syscall_set_result(frame, status);
            free(data);
        }
    }
}

void syscall_NET_SOCKET_AVAILABLE(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;

    syscall_set_result(frame, net_socket_available((int)arg0));
}

void syscall_NET_SOCKET_CLOSE(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;

    syscall_set_result(frame, net_socket_close((int)arg0));
}

void syscall_CLEAR_SCREEN(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    (void)regs;

    clear_screen();
    syscall_set_result(frame, 0);
}

void syscall_RTC_GET_LOCAL(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;

    rtc_local_time_t out_time;
    int status;

    read_rtc();
    out_time.year = (uint16_t)get_year();
    out_time.month = get_month();
    out_time.day = get_day();
    out_time.hour = get_hour();
    out_time.minute = get_minute();
    out_time.second = get_second();
    status = copy_to_user((void*)arg0, &out_time, sizeof(out_time)) == 0 ? 0 : -1;
    syscall_set_result(frame, status);
}

void syscall_RTC_GET_TZ_OFFSET(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    (void)regs;

    syscall_set_result(frame, rtc_get_timezone_offset_minutes());
}

void syscall_RTC_SET_TZ_OFFSET(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;

    rtc_set_timezone_offset_minutes((int)arg0);
    syscall_set_result(frame, 0);
}

void syscall_RTC_SAVE_TZ(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    (void)regs;

    syscall_set_result(frame, rtc_save_timezone_setting());
}

void syscall_PRIV_CMD(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;
    uintptr_t arg1 = (uintptr_t)regs->arg1;

    char arg[SYSCALL_USER_PATH_MAX];

    if (arg1 == 0U) {
        syscall_set_result(frame, kernel_run_privileged_command((int)arg0, 0));
    } else if (copy_string_from_user(arg, (const char*)arg1, sizeof(arg)) != 0) {
        syscall_set_result(frame, -1);
    } else {
        syscall_set_result(frame, kernel_run_privileged_command((int)arg0, arg));
    }
}

void syscall_PRINT_RAW(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;

    char text[SYSCALL_USER_TEXT_MAX];

    if (copy_string_from_user(text, (const char*)arg0, sizeof(text)) != 0) syscall_set_result(frame, -1);
    else {
        vga_print(text);
        syscall_set_result(frame, 0);
    }
}

void syscall_GUI_OPEN_NARCPAD_FILE(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;

    char path[SYSCALL_USER_PATH_MAX];

    syscall_set_result(frame, copy_string_from_user(path, (const char*)arg0, sizeof(path)) == 0
                                  ? kernel_gui_open_narcpad_file(path)
                                  : -1);
}

void syscall_SPAWN(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;
    uintptr_t arg1 = (uintptr_t)regs->arg1;
    uintptr_t arg2 = (uintptr_t)regs->arg2;

    char path[SYSCALL_USER_PATH_MAX];
    char argv_copy[PROCESS_MAX_ARGS][PROCESS_MAX_ARG_LEN];
    const char* argv_ptrs[PROCESS_MAX_ARGS];
    uint8_t argv_slot_size = current_user_argv_slot_size();

    if (arg2 > PROCESS_MAX_ARGS || copy_string_from_user(path, (const char*)arg0, sizeof(path)) != 0) {
        syscall_set_result(frame, -1);
    } else if (arg2 != 0U &&
               copy_argv_from_user(arg1, (uint32_t)arg2, argv_slot_size, argv_copy, argv_ptrs) != 0) {
        syscall_set_result(frame, -1);
    } else {
        syscall_set_result(frame, process_create_user(path, arg2 != 0U ? argv_ptrs : 0, (int)arg2, 0U));
    }
}

void syscall_EXEC(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;
    uintptr_t arg1 = (uintptr_t)regs->arg1;
    uintptr_t arg2 = (uintptr_t)regs->arg2;

    char path[SYSCALL_USER_PATH_MAX];
    char argv_copy[PROCESS_MAX_ARGS][PROCESS_MAX_ARG_LEN];
    const char* argv_ptrs[PROCESS_MAX_ARGS];
    uint8_t argv_slot_size = current_user_argv_slot_size();

    if (arg2 > PROCESS_MAX_ARGS || copy_string_from_user(path, (const char*)arg0, sizeof(path)) != 0) {
        syscall_set_result(frame, -1);
    } else if (arg2 != 0U &&
               copy_argv_from_user(arg1, (uint32_t)arg2, argv_slot_size, argv_copy, argv_ptrs) != 0) {
        syscall_set_result(frame, -1);
    } else if (process_request_exec_current(path, arg2 != 0U ? argv_ptrs : 0, (int)arg2) != 0) {
        syscall_set_result(frame, -1);
    } else {
        user_kernel_return_mode = USER_KERNEL_RETURN_KERNEL;
        syscall_set_result(frame, 0);
    }
}

void syscall_WAITPID(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;
    uintptr_t arg1 = (uintptr_t)regs->arg1;
    uintptr_t arg2 = (uintptr_t)regs->arg2;

    process_t* current = process_current();
    int child_status = 0;
    int status;

    if (current && current->kind == PROCESS_KIND_USER) {
        if (process_request_wait_current((int)arg0, arg1, (uint32_t)arg2) != 0) {
            syscall_set_result(frame, -1);
        } else {
            user_kernel_return_mode = USER_KERNEL_RETURN_KERNEL;
            syscall_set_result(frame, 0);
        }
    } else {
        status = process_waitpid_sync_current((int)arg0, (uint32_t)arg2, &child_status);
        if (status > 0 && arg1 != 0U && copy_to_user((void*)arg1, &child_status, sizeof(child_status)) != 0) {
            status = -1;
        }
        syscall_set_result(frame, status);
    }
}

void syscall_KILL(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;

    syscall_set_result(frame, process_kill_pid((int)arg0));
}

void syscall_SLEEP(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;

    if (arg0 == 0U) {
        syscall_set_result(frame, 0);
    } else if (process_request_sleep_current((uint32_t)arg0) != 0) {
        syscall_set_result(frame, -1);
    } else {
        user_kernel_return_mode = USER_KERNEL_RETURN_KERNEL;
        syscall_set_result(frame, 0);
    }
}

void syscall_READ(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;
    uintptr_t arg1 = (uintptr_t)regs->arg1;
    uintptr_t arg2 = (uintptr_t)regs->arg2;

    process_t* current = process_current();
    int status;

    if (!current) syscall_set_result(frame, -1);
    else if (arg2 == 0U) syscall_set_result(frame, 0);
    else if (current->kind == PROCESS_KIND_USER) {
        if (process_request_read_current((int)arg0, arg1, (uint32_t)arg2) != 0) {
            syscall_set_result(frame, -1);
        } else {
            user_kernel_return_mode = USER_KERNEL_RETURN_KERNEL;
            syscall_set_result(frame, 0);
        }
    } else {
        void* buffer = malloc((size_t)arg2);

        if (!buffer) syscall_set_result(frame, -1);
        else {
            status = fd_read(current, (int)arg0, buffer, (uint32_t)arg2);
            if (status > 0) {
                reactivate_current_user_space();
                if (copy_to_user((void*)arg1, buffer, (uint32_t)status) != 0) status = -1;
            }
            syscall_set_result(frame, status);
            free(buffer);
        }
    }
}

void syscall_WRITE(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;
    uintptr_t arg1 = (uintptr_t)regs->arg1;
    uintptr_t arg2 = (uintptr_t)regs->arg2;

    process_t* current = process_current();
    int status;

    if (!current) syscall_set_result(frame, -1);
    else if (arg2 == 0U) syscall_set_result(frame, 0);
    else if (current->kind == PROCESS_KIND_USER) {
        if (process_request_write_current((int)arg0, arg1, (uint32_t)arg2) != 0) {
            syscall_set_result(frame, -1);
        } else {
            user_kernel_return_mode = USER_KERNEL_RETURN_KERNEL;
            syscall_set_result(frame, 0);
        }
    } else {
        void* buffer = malloc((size_t)arg2);

        if (!buffer) syscall_set_result(frame, -1);
        else {
            status = copy_from_user(buffer, (const void*)arg1, (uint32_t)arg2) == 0
                         ? fd_write(current, (int)arg0, buffer, (uint32_t)arg2)
                         : -1;
            syscall_set_result(frame, status);
            free(buffer);
        }
    }
}

void syscall_CLOSE(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;

    syscall_set_result(frame, fd_close(process_current(), (int)arg0));
}

void syscall_DUP2(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;
    uintptr_t arg1 = (uintptr_t)regs->arg1;

    syscall_set_result(frame, fd_dup2(process_current(), (int)arg0, (int)arg1));
}

void syscall_PIPE(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;
    uintptr_t arg1 = (uintptr_t)regs->arg1;

    process_t* current = process_current();
    int pair[2];
    int status;

    if (!current) syscall_set_result(frame, -1);
    else if (fd_pipe(current, pair) != 0) syscall_set_result(frame, -1);
    else {
        reactivate_current_user_space();
        status = copy_to_user((void*)arg0, pair, sizeof(pair)) == 0 ? 0 : -1;
        if (status != 0) {
            (void)fd_close(current, pair[0]);
            (void)fd_close(current, pair[1]);
        }
        syscall_set_result(frame, status);
    }
}

void syscall_PROCESS_SNAPSHOT(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;
    uintptr_t arg1 = (uintptr_t)regs->arg1;

    process_snapshot_entry_t entries[16];
    int status;

    if ((int)arg1 <= 0 || arg1 > (uintptr_t)(sizeof(entries) / sizeof(entries[0]))) {
        syscall_set_result(frame, -1);
    }
    else {
        status = process_snapshot(entries, (int)arg1);
        if (status > 0) {
            reactivate_current_user_space();
            if (copy_to_user((void*)arg0, entries, (uint32_t)status * (uint32_t)sizeof(entries[0])) != 0) {
                status = -1;
            }
        }
        syscall_set_result(frame, status);
    }
}

void syscall_EXIT(arch_trap_frame_t *frame, arch_syscall_state_t *regs) {
    uintptr_t arg0 = (uintptr_t)regs->arg0;

    if (usermode_schedule_current_process_exit((int)arg0) == 0) {
        syscall_set_result(frame, 0);
    } else if (usermode_exit_current_task((int)arg0) == 0) {
        syscall_set_result(frame, 0);
    } else {
        process_exit_current((int)arg0);
    }
}

static syscall_handler_routine syscalltab[] = {
    syscall_EXIT,
    syscall_PRINT,
    syscall_MALLOC,
    syscall_FREE,
    syscall_GUI_UPDATE,
    syscall_YIELD,
    syscall_UPTIME,
    syscall_GETPID,
    syscall_CHDIR,
    syscall_FS_READ,
    syscall_FS_WRITE,
    syscall_SNAKE_GET_INPUT,
    syscall_SNAKE_CLOSE,
    syscall_RANDOM,
    syscall_NET_GET_CONFIG,
    syscall_NET_RESOLVE,
    syscall_NET_NTP_QUERY,
    syscall_NET_SOCKET_OPEN,
    syscall_NET_SOCKET_CONNECT,
    syscall_NET_SOCKET_SEND,
    syscall_NET_SOCKET_RECV,
    syscall_NET_SOCKET_AVAILABLE,
    syscall_NET_SOCKET_CLOSE,
    syscall_FS_LIST,
    syscall_FS_GET_CWD,
    syscall_FS_TOUCH,
    syscall_FS_MKDIR,
    syscall_FS_DELETE,
    syscall_FS_MOVE,
    syscall_FS_RENAME,
    syscall_CLEAR_SCREEN,
    syscall_RTC_GET_LOCAL,
    syscall_NET_DHCP,
    syscall_NET_PING,
    syscall_PRIV_CMD,
    syscall_PRINT_RAW,
    syscall_FS_FIND_NODE,
    syscall_FS_GET_NODE_INFO,
    syscall_FS_GET_PATH,
    syscall_RTC_GET_TZ_OFFSET,
    syscall_RTC_SET_TZ_OFFSET,
    syscall_RTC_SAVE_TZ,
    syscall_GUI_OPEN_NARCPAD_FILE,
    syscall_GETRANDOM,
    syscall_FS_READ_RAW,
    syscall_FS_WRITE_RAW,
    syscall_SPAWN,
    syscall_EXEC,
    syscall_WAITPID,
    syscall_KILL,
    syscall_GETPPID,
    syscall_SLEEP,
    syscall_READ,
    syscall_WRITE,
    syscall_CLOSE,
    syscall_DUP2,
    syscall_PIPE,
    syscall_PROCESS_SNAPSHOT,
};

void syscall_handler(arch_trap_frame_t* frame) {
    arch_syscall_state_t regs;
    uint32_t syscall_num;

    arch_syscall_capture(frame, &regs);
    syscall_num = (uint32_t)regs.number;

    if (syscall_num < sizeof(syscalltab) / sizeof(syscall_handler_routine)) {
        syscall_handler_routine routine = syscalltab[syscall_num];
        routine(frame, &regs);
    } else {
        syscall_set_result(frame, 0);
    }

    if (user_kernel_return_mode == USER_KERNEL_RETURN_KERNEL) {
        process_t* current = process_current();

        if (current && current->kind == PROCESS_KIND_USER) {
            current->arch.user_frame = *frame;
        }
    } else {
        arch_set_kernel_stack(usermode_active_trap_stack_top());
    }
}
