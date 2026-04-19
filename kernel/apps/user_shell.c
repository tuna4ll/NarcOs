#include <stdint.h>
#include "string.h"
#include "fs.h"
#include "net.h"
#include "syscall.h"
#include "usermode.h"
#include "user_abi.h"
#include "user_net_apps.h"
#include "user_tls.h"

#define USER_CODE __attribute__((section(".user_code")))
#define USER_RODATA __attribute__((section(".user_rodata")))

#include "user_string.h"

#define strcmp user_strcmp
#define strncmp user_strncmp
#define strlen user_strlen
#define strncpy user_strncpy
#define memset user_memset

static const char shell_help_lines[][72] USER_RODATA = {
    "NarcOs Shell",
    "  help    - Show this menu",
    "  clear   - Clear the screen",
    "  mem     - Memory map",
    "  snake   - Snake game (requires graphics mode)",
    "  settings - Open settings (requires graphics mode)",
    "  ver     - Show version",
    "  uptime  - Show system uptime in seconds",
    "  date    - Show current local date",
    "  time    - Show current local time",
    "  ls      - List files",
    "  pwd     - Show current path",
    "  touch   - Create empty file (touch <file>)",
    "  cat     - Read file (cat <file>)",
    "  write   - Write to file (write <file> <text>)",
    "  edit    - Open file in NarcVim (edit <file>)",
    "  mkdir   - Create directory (mkdir <name>)",
    "  cd      - Change directory (cd <name> or cd ..)",
    "  rm      - Delete file (rm <file>)",
    "  mv      - Move item (mv <src> <target-dir>)",
    "  ren     - Rename item (ren <path> <new-name>)",
    "  net     - Show network status",
    "  dhcp    - Request IPv4 configuration",
    "  dns     - Resolve hostname to IPv4",
    "  ping    - Ping an IPv4 host",
    "  ntp     - Query UTC time from an NTP server",
    "  http    - Fetch HTTP/1.0 response (http <host> [path])",
    "  https   - Fetch HTTPS response (https https://host/path)",
    "  netdemo - Run Ring 3 HTTP demo (netdemo <host> [path])",
    "  fetch   - Download HTTP/HTTPS body to a file",
    "  tls_test - Run TLS userland self-tests",
    "  hwinfo  - Show hardware summary",
    "  pci     - List PCI devices",
    "  storage - List storage controllers, partitions and active backend",
    "  log     - Show kernel ring log",
    "  reboot  - Reboot system",
    "  poweroff - Power off system",
    "  malloc_test - Test dynamic heap memory",
    "  usermode_test - Test Ring 3 transition and syscall"
};

static const char msg_success[] USER_RODATA = "Success.";
static const char msg_unknown_prefix[] USER_RODATA = "Error: Unknown command '";
static const char msg_unknown_suffix[] USER_RODATA = "'";
static const char msg_empty_response[] USER_RODATA = "(empty response)";
static const char msg_http_truncated[] USER_RODATA = "warning: Response truncated to local buffer size.";
static const char msg_http_partial[] USER_RODATA = "warning: Remote peer did not close cleanly before timeout.";
static const char msg_netdemo_start[] USER_RODATA = "Ring3 netdemo: HTTP request starting";
static const char msg_netdemo_err[] USER_RODATA = "Ring3 netdemo: HTTP request failed";
static const char msg_netdemo_ok[] USER_RODATA = "Ring3 netdemo: response";
static const char msg_netdemo_truncated[] USER_RODATA = "Ring3 netdemo: response truncated";
static const char msg_netdemo_partial[] USER_RODATA = "Ring3 netdemo: connection timed out before clean close";
static const char msg_fetch_start[] USER_RODATA = "Ring3 fetch: downloading";
static const char msg_fetch_write_err[] USER_RODATA = "Ring3 fetch: failed to write file";
static const char msg_tls_test_header[] USER_RODATA = "TLS Self-Test";
static const char msg_tls_test_case_prefix[] USER_RODATA = "[";
static const char msg_tls_test_case_sep[] USER_RODATA = "] ";
static const char msg_tls_test_dash[] USER_RODATA = " - ";

static USER_CODE const char* shell_net_error_string(int status) {
    switch (status) {
        case NET_ERR_INVALID: return "invalid request";
        case NET_ERR_UNSUPPORTED: return "unsupported";
        case NET_ERR_NOT_READY: return "network not ready";
        case NET_ERR_NO_SOCKETS: return "no sockets available";
        case NET_ERR_RESOLVE: return "dns resolve failed";
        case NET_ERR_TIMEOUT: return "timeout";
        case NET_ERR_STATE: return "invalid socket state";
        case NET_ERR_RESET: return "connection reset";
        case NET_ERR_CLOSED: return "connection closed";
        case NET_ERR_WOULD_BLOCK: return "would block";
        case NET_ERR_IO: return "i/o error";
        case NET_ERR_OVERFLOW: return "buffer overflow";
        default: return "unknown error";
    }
}

static USER_CODE void shell_println(const char* text) {
    user_print(text ? text : "");
}

static USER_CODE void shell_print_usage(const char* usage) {
    shell_println(usage);
}

static USER_CODE void shell_print_error(const char* message) {
    char line[160];
    int off = 0;

    if (!message) message = "error";
    while (*message && off + 1 < (int)sizeof(line)) line[off++] = *message++;
    line[off] = '\0';
    shell_println(line);
}

static USER_CODE int shell_append_text(char* dst, int dst_len, int* io_off, const char* src) {
    int off = *io_off;

    if (!dst || !io_off || !src || dst_len <= 0) return -1;
    while (*src) {
        if (off + 1 >= dst_len) return -1;
        dst[off++] = *src++;
    }
    dst[off] = '\0';
    *io_off = off;
    return 0;
}

static USER_CODE int shell_append_char(char* dst, int dst_len, int* io_off, char c) {
    int off = *io_off;

    if (!dst || !io_off || dst_len <= 0 || off + 1 >= dst_len) return -1;
    dst[off++] = c;
    dst[off] = '\0';
    *io_off = off;
    return 0;
}

static USER_CODE int shell_append_uint(char* dst, int dst_len, int* io_off, uint32_t value) {
    char digits[16];
    int digit_count = 0;

    if (value == 0U) return shell_append_char(dst, dst_len, io_off, '0');
    while (value != 0U && digit_count < (int)sizeof(digits)) {
        digits[digit_count++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    for (int i = digit_count - 1; i >= 0; i--) {
        if (shell_append_char(dst, dst_len, io_off, digits[i]) != 0) return -1;
    }
    return 0;
}

static USER_CODE int shell_append_ip(char* dst, int dst_len, int* io_off, uint32_t ip_addr) {
    for (int i = 0; i < 4; i++) {
        uint32_t octet = (ip_addr >> (24 - i * 8)) & 0xFFU;
        if (shell_append_uint(dst, dst_len, io_off, octet) != 0) return -1;
        if (i != 3 && shell_append_char(dst, dst_len, io_off, '.') != 0) return -1;
    }
    return 0;
}

static USER_CODE void shell_print_ip_line(const char* label, uint32_t ip_addr) {
    char line[96];
    int off = 0;

    line[0] = '\0';
    (void)shell_append_text(line, sizeof(line), &off, label);
    (void)shell_append_ip(line, sizeof(line), &off, ip_addr);
    shell_println(line);
}

static USER_CODE const char* shell_skip_spaces(const char* text) {
    while (text && *text == ' ') text++;
    return text;
}

static USER_CODE const char* shell_find_arg_tail(const char* text) {
    text = shell_skip_spaces(text);
    while (*text && *text != ' ') text++;
    return shell_skip_spaces(text);
}

static USER_CODE int shell_copy_token(const char* src, char* out, int out_len) {
    int len = 0;

    if (!src || !out || out_len <= 1) return -1;
    src = shell_skip_spaces(src);
    if (*src == '\0') {
        out[0] = '\0';
        return -1;
    }
    while (*src && *src != ' ') {
        if (len + 1 >= out_len) return -1;
        out[len++] = *src++;
    }
    out[len] = '\0';
    return 0;
}

static USER_CODE int shell_copy_remainder(const char* src, char* out, int out_len) {
    int len = 0;

    if (!src || !out || out_len <= 0) return -1;
    src = shell_skip_spaces(src);
    while (*src) {
        if (len + 1 >= out_len) return -1;
        out[len++] = *src++;
    }
    out[len] = '\0';
    return len > 0 ? 0 : -1;
}

static USER_CODE int shell_parse_two_args(const char* args, char* first, int first_len,
                                          char* second, int second_len) {
    const char* tail;

    if (shell_copy_token(args, first, first_len) != 0) return -1;
    tail = shell_find_arg_tail(args);
    if (shell_copy_remainder(tail, second, second_len) != 0) return -1;
    return 0;
}

static USER_CODE int shell_parse_http_target(const char* target, char* host, int host_len,
                                             char* path, int path_len) {
    const char* cursor = shell_skip_spaces(target);
    int host_off = 0;
    int path_off = 0;

    if (!cursor || !host || !path || host_len <= 1 || path_len <= 1) return -1;
    if (strncmp(cursor, "http://", 7) == 0) cursor += 7;
    else if (strncmp(cursor, "https://", 8) == 0) cursor += 8;

    while (*cursor && *cursor != ' ' && *cursor != '/') {
        if (host_off + 1 >= host_len) return -1;
        host[host_off++] = *cursor++;
    }
    host[host_off] = '\0';
    if (host_off == 0) return -1;

    if (*cursor == '/') {
        while (*cursor && *cursor != ' ') {
            if (path_off + 1 >= path_len) return -1;
            path[path_off++] = *cursor++;
        }
    } else {
        cursor = shell_skip_spaces(cursor);
        if (*cursor != '\0') {
            if (*cursor != '/') {
                if (path_off + 1 >= path_len) return -1;
                path[path_off++] = '/';
            }
            while (*cursor) {
                if (path_off + 1 >= path_len) return -1;
                path[path_off++] = *cursor++;
            }
        }
    }

    if (path_off == 0) path[path_off++] = '/';
    path[path_off] = '\0';
    return 0;
}

static USER_CODE int shell_parse_fetch_args(const char* args, char* host, int host_len,
                                            char* path, int path_len, char* output_path, int output_path_len,
                                            int* out_use_https) {
    char first[128];
    char second[128];
    char third[128];
    const char* tail0;
    const char* tail1;

    if (out_use_https) *out_use_https = 0;
    if (shell_copy_token(args, first, sizeof(first)) != 0) return -1;
    tail0 = shell_find_arg_tail(args);
    if (shell_copy_token(tail0, second, sizeof(second)) != 0) return -1;
    tail1 = shell_find_arg_tail(tail0);

    if (*tail1 == '\0') {
        if (strncmp(first, "https://", 8) == 0 && out_use_https) *out_use_https = 1;
        if (shell_parse_http_target(first, host, host_len, path, path_len) != 0) return -1;
        strncpy(output_path, second, (size_t)(output_path_len - 1));
        output_path[output_path_len - 1] = '\0';
        return 0;
    }

    if (shell_copy_token(tail1, third, sizeof(third)) != 0) return -1;
    if (shell_find_arg_tail(tail1)[0] != '\0') return -1;

    {
        const char* host_token = first;
        if (strncmp(host_token, "http://", 7) == 0) host_token += 7;
        else if (strncmp(host_token, "https://", 8) == 0) {
            host_token += 8;
            if (out_use_https) *out_use_https = 1;
        }
        strncpy(host, host_token, (size_t)(host_len - 1));
    }
    host[host_len - 1] = '\0';
    strncpy(path, second, (size_t)(path_len - 1));
    path[path_len - 1] = '\0';
    if (path[0] != '/') return -1;
    strncpy(output_path, third, (size_t)(output_path_len - 1));
    output_path[output_path_len - 1] = '\0';
    return 0;
}

static USER_CODE void shell_format_datetime(char* out, int out_len, const rtc_local_time_t* t, int date_only) {
    int off = 0;

    if (!out || out_len <= 0 || !t) return;
    out[0] = '\0';
    (void)shell_append_text(out, out_len, &off, "20");
    (void)shell_append_uint(out, out_len, &off, t->year);
    (void)shell_append_char(out, out_len, &off, '-');
    if (t->month < 10U) (void)shell_append_char(out, out_len, &off, '0');
    (void)shell_append_uint(out, out_len, &off, t->month);
    (void)shell_append_char(out, out_len, &off, '-');
    if (t->day < 10U) (void)shell_append_char(out, out_len, &off, '0');
    (void)shell_append_uint(out, out_len, &off, t->day);
    if (!date_only) {
        (void)shell_append_char(out, out_len, &off, ' ');
        if (t->hour < 10U) (void)shell_append_char(out, out_len, &off, '0');
        (void)shell_append_uint(out, out_len, &off, t->hour);
        (void)shell_append_char(out, out_len, &off, ':');
        if (t->minute < 10U) (void)shell_append_char(out, out_len, &off, '0');
        (void)shell_append_uint(out, out_len, &off, t->minute);
        (void)shell_append_char(out, out_len, &off, ':');
        if (t->second < 10U) (void)shell_append_char(out, out_len, &off, '0');
        (void)shell_append_uint(out, out_len, &off, t->second);
    }
}

static USER_CODE void shell_unix_to_utc(uint32_t unix_seconds, rtc_local_time_t* out) {
    uint32_t day_seconds = unix_seconds % 86400U;
    int z = (int)(unix_seconds / 86400U);
    int era;
    unsigned doe;
    unsigned yoe;
    unsigned doy;
    unsigned mp;
    int year;

    if (!out) return;

    out->hour = (uint8_t)(day_seconds / 3600U);
    day_seconds %= 3600U;
    out->minute = (uint8_t)(day_seconds / 60U);
    out->second = (uint8_t)(day_seconds % 60U);

    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = (unsigned)(z - era * 146097);
    yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
    year = (int)yoe + era * 400;
    doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
    mp = (5U * doy + 2U) / 153U;
    out->day = (uint8_t)(doy - (153U * mp + 2U) / 5U + 1U);
    out->month = (uint8_t)((int)mp + (mp < 10U ? 3 : -9));
    year += (out->month <= 2U);
    out->year = (uint16_t)(year - 2000);
}

static USER_CODE void shell_run_help(void) {
    uint32_t count = (uint32_t)(sizeof(shell_help_lines) / sizeof(shell_help_lines[0]));
    for (uint32_t i = 0; i < count; i++) shell_println(shell_help_lines[i]);
}

static USER_CODE int shell_run_ls(user_shell_state_t* state) {
    int count;

    if (!state) return -1;
    count = user_fs_list(state->dir_entries, MAX_FILES);
    if (count < 0) {
        shell_print_error("error: Failed to read directory.");
        return -1;
    }

    shell_println("Name\t\tSize (Bytes)");
    shell_println("----------------------------");
    for (int i = 0; i < count; i++) {
        char line[96];
        int off = 0;

        line[0] = '\0';
        (void)shell_append_text(line, sizeof(line), &off, state->dir_entries[i].name);
        if (state->dir_entries[i].flags == FS_NODE_DIR) {
            (void)shell_append_text(line, sizeof(line), &off, "/\t\t<DIR>");
        } else {
            (void)shell_append_char(line, sizeof(line), &off, '\t');
            if (strlen(state->dir_entries[i].name) < 8U) (void)shell_append_char(line, sizeof(line), &off, '\t');
            (void)shell_append_uint(line, sizeof(line), &off, state->dir_entries[i].size);
        }
        shell_println(line);
    }
    return 0;
}

static USER_CODE int shell_run_pwd(user_shell_state_t* state) {
    if (!state) return -1;
    if (user_fs_get_cwd(state->scratch, sizeof(state->scratch)) != 0) {
        shell_print_error("error: Failed to get current path.");
        return -1;
    }
    shell_println(state->scratch);
    return 0;
}

static USER_CODE int shell_run_touch(const char* arg) {
    if (!arg || arg[0] == '\0') {
        shell_print_usage("Usage: touch <file>");
        return -1;
    }
    if (user_fs_touch(arg) != 0) {
        shell_print_error("error: Failed to create file.");
        return -1;
    }
    shell_println(msg_success);
    return 0;
}

static USER_CODE int shell_run_cat(user_shell_state_t* state, const char* arg) {
    if (!state) return -1;
    if (!arg || arg[0] == '\0') {
        shell_print_usage("Usage: cat <file>");
        return -1;
    }
    if (user_fs_read(arg, state->scratch, sizeof(state->scratch)) != 0) {
        shell_print_error("error: File not found.");
        return -1;
    }
    shell_println(state->scratch);
    return 0;
}

static USER_CODE int shell_run_write(const char* args) {
    char file_name[64];
    const char* content;

    if (shell_copy_token(args, file_name, sizeof(file_name)) != 0) {
        shell_print_usage("Usage: write <file> <text>");
        return -1;
    }
    content = shell_find_arg_tail(args);
    if (!content || *content == '\0') {
        shell_print_error("error: Text cannot be empty.");
        return -1;
    }
    if (user_fs_write(file_name, content) != 0) {
        shell_print_error("error: Failed to create file or not enough space.");
        return -1;
    }
    shell_println(msg_success);
    return 0;
}

static USER_CODE int shell_run_mkdir(const char* arg) {
    if (!arg || arg[0] == '\0') {
        shell_print_usage("Usage: mkdir <name>");
        return -1;
    }
    if (user_fs_mkdir(arg) != 0) {
        shell_print_error("error: Failed to create directory.");
        return -1;
    }
    shell_println("Directory created.");
    return 0;
}

static USER_CODE int shell_run_cd(const char* arg) {
    if (!arg || arg[0] == '\0') {
        shell_print_usage("Usage: cd <name>");
        return -1;
    }
    if (user_syscall1(SYS_CHDIR, (uint32_t)arg) != 0) {
        shell_print_error("error: Directory not found.");
        return -1;
    }
    return 0;
}

static USER_CODE int shell_run_rm(const char* arg) {
    if (!arg || arg[0] == '\0') {
        shell_print_usage("Usage: rm <file>");
        return -1;
    }
    if (user_fs_delete(arg) != 0) {
        shell_print_error("error: Item not found or directory not empty.");
        return -1;
    }
    shell_println("Success. Item deleted.");
    return 0;
}

static USER_CODE int shell_run_mv(const char* args) {
    char src[128];
    char dst[128];

    if (shell_parse_two_args(args, src, sizeof(src), dst, sizeof(dst)) != 0) {
        shell_print_usage("Usage: mv <src> <target-dir>");
        return -1;
    }
    if (user_fs_move(src, dst) != 0) {
        shell_print_error("error: Move failed.");
        return -1;
    }
    shell_println(msg_success);
    return 0;
}

static USER_CODE int shell_run_ren(const char* args) {
    char src[128];
    char name[64];

    if (shell_parse_two_args(args, src, sizeof(src), name, sizeof(name)) != 0) {
        shell_print_usage("Usage: ren <path> <new-name>");
        return -1;
    }
    if (user_fs_rename(src, name) != 0) {
        shell_print_error("error: Rename failed.");
        return -1;
    }
    shell_println(msg_success);
    return 0;
}

static USER_CODE int shell_run_local_date(user_shell_state_t* state, int date_only) {
    char line[64];

    if (!state) return -1;
    if (user_get_local_time(&state->local_time) != 0) {
        shell_print_error("error: Failed to read RTC.");
        return -1;
    }
    line[0] = '\0';
    shell_format_datetime(line, sizeof(line), &state->local_time, date_only);
    if (date_only) {
        char prefixed[96];
        int off = 0;
        prefixed[0] = '\0';
        (void)shell_append_text(prefixed, sizeof(prefixed), &off, "Current Local Date: ");
        (void)shell_append_text(prefixed, sizeof(prefixed), &off, line);
        shell_println(prefixed);
    } else {
        char prefixed[96];
        int off = 0;
        prefixed[0] = '\0';
        (void)shell_append_text(prefixed, sizeof(prefixed), &off, "Current Local Time: ");
        (void)shell_append_text(prefixed, sizeof(prefixed), &off, line + 11);
        shell_println(prefixed);
    }
    return 0;
}

static USER_CODE int shell_run_net_status(void) {
    net_ipv4_config_t config;

    memset(&config, 0, sizeof(config));
    if (user_net_get_config(&config) != 0 || !config.available) {
        shell_print_error("Network: driver not ready.");
        return -1;
    }

    shell_println("Network Driver : RTL8139");
    shell_println(config.configured ? "Address Mode   : DHCP" : "Address Mode   : Unconfigured");
    if (config.configured) {
        shell_print_ip_line("IP             : ", config.ip_addr);
        shell_print_ip_line("Netmask        : ", config.netmask);
        shell_print_ip_line("Gateway        : ", config.gateway);
        shell_print_ip_line("DNS            : ", config.dns_server);
    }
    return 0;
}

static USER_CODE int shell_run_dhcp(void) {
    net_ipv4_config_t config;

    if (user_net_dhcp() != 0) {
        shell_print_error("error: DHCP request failed.");
        return -1;
    }
    shell_println("DHCP lease acquired.");
    memset(&config, 0, sizeof(config));
    if (user_net_get_config(&config) == 0 && config.configured) {
        shell_print_ip_line("IP             : ", config.ip_addr);
        shell_print_ip_line("Gateway        : ", config.gateway);
        shell_print_ip_line("DNS            : ", config.dns_server);
    }
    return 0;
}

static USER_CODE int shell_run_dns(const char* arg) {
    uint32_t ip_addr = 0;
    char line[160];
    int off = 0;

    if (!arg || arg[0] == '\0') {
        shell_print_usage("Usage: dns <host>");
        return -1;
    }
    if (user_net_resolve(arg, &ip_addr) != NET_OK) {
        shell_print_error("error: DNS lookup failed.");
        return -1;
    }

    line[0] = '\0';
    (void)shell_append_text(line, sizeof(line), &off, arg);
    (void)shell_append_text(line, sizeof(line), &off, " -> ");
    (void)shell_append_ip(line, sizeof(line), &off, ip_addr);
    shell_println(line);
    return 0;
}

static USER_CODE int shell_run_ping(user_shell_state_t* state, const char* arg) {
    char line[160];
    int status;

    if (!state) return -1;
    if (!arg || arg[0] == '\0') {
        shell_print_usage("Usage: ping <host>");
        return -1;
    }

    status = user_net_ping(arg, &state->ping_result);
    if (status == NET_ERR_NOT_READY) {
        shell_print_error("error: Network driver is not ready.");
        return -1;
    }
    if (status == NET_ERR_RESOLVE) {
        shell_print_error("error: Failed to resolve target host.");
        return -1;
    }
    if (status == NET_ERR_IO) {
        shell_print_error("error: Failed to transmit ICMP packet.");
        return -1;
    }

    line[0] = '\0';
    {
        int off = 0;
        (void)shell_append_text(line, sizeof(line), &off, "Pinging ");
        (void)shell_append_text(line, sizeof(line), &off, arg);
        (void)shell_append_text(line, sizeof(line), &off, " [");
        (void)shell_append_ip(line, sizeof(line), &off, state->ping_result.resolved_ip);
        (void)shell_append_text(line, sizeof(line), &off, "] ...");
    }
    shell_println(line);

    for (uint32_t i = 0; i < state->ping_result.attempts; i++) {
        if (state->ping_result.reply_status[i] == NET_OK) {
            int off = 0;
            line[0] = '\0';
            (void)shell_append_text(line, sizeof(line), &off, "Reply from ");
            (void)shell_append_ip(line, sizeof(line), &off, state->ping_result.resolved_ip);
            (void)shell_append_text(line, sizeof(line), &off, ": time=");
            (void)shell_append_uint(line, sizeof(line), &off, state->ping_result.rtt_ms[i]);
            (void)shell_append_text(line, sizeof(line), &off, "ms");
            shell_println(line);
        } else {
            shell_println("Request timed out.");
        }
    }
    return status == NET_OK ? 0 : -1;
}

static USER_CODE int shell_run_ntp(user_shell_state_t* state, const char* arg) {
    uint32_t unix_seconds = 0;
    const char* host = (arg && arg[0] != '\0') ? arg : user_tls_default_ntp_host();
    char line[160];
    rtc_local_time_t utc_time;
    int status;
    int off = 0;

    if (!state) return -1;
    status = user_tls_get_utc_unix_time_for_host((arg && arg[0] != '\0') ? arg : (const char*)0, &unix_seconds);
    if (status != NET_OK) {
        line[0] = '\0';
        off = 0;
        (void)shell_append_text(line, sizeof(line), &off, "error: NTP query failed: ");
        (void)shell_append_text(line, sizeof(line), &off, shell_net_error_string(status));
        shell_print_error(line);
        return -1;
    }

    shell_unix_to_utc(unix_seconds, &utc_time);
    line[0] = '\0';
    off = 0;
    (void)shell_append_text(line, sizeof(line), &off, "NTP server      : ");
    (void)shell_append_text(line, sizeof(line), &off, host);
    shell_println(line);

    shell_format_datetime(state->aux, sizeof(state->aux), &utc_time, 0);
    line[0] = '\0';
    off = 0;
    (void)shell_append_text(line, sizeof(line), &off, "UTC time        : ");
    (void)shell_append_text(line, sizeof(line), &off, state->aux);
    shell_println(line);

    line[0] = '\0';
    off = 0;
    (void)shell_append_text(line, sizeof(line), &off, "Unix seconds    : ");
    (void)shell_append_uint(line, sizeof(line), &off, unix_seconds);
    shell_println(line);
    return 0;
}

static USER_CODE int shell_print_http_response(const char* target, const char* response, const net_http_result_t* result) {
    char line[224];
    int off = 0;

    line[0] = '\0';
    (void)shell_append_text(line, sizeof(line), &off, "HTTP GET        : ");
    (void)shell_append_text(line, sizeof(line), &off, target);
    shell_println(line);

    line[0] = '\0';
    off = 0;
    (void)shell_append_text(line, sizeof(line), &off, "Resolved        : ");
    (void)shell_append_ip(line, sizeof(line), &off, result->resolved_ip);
    shell_println(line);

    shell_println("---- response ----");
    shell_println((response && response[0] != '\0') ? response : msg_empty_response);
    if (result->truncated != 0U) shell_println(msg_http_truncated);
    if (result->complete == 0U) shell_println(msg_http_partial);
    return 0;
}

static USER_CODE int shell_run_http(user_shell_state_t* state, const char* arg) {
    char host[96];
    char path[160];
    int status;

    if (!state) return -1;
    if (!arg || arg[0] == '\0') {
        shell_print_usage("Usage: http <host> [path]");
        return -1;
    }
    if (shell_parse_http_target(arg, host, sizeof(host), path, sizeof(path)) != 0) {
        shell_print_usage("Usage: http <host> [path]");
        return -1;
    }
    status = user_http_fetch_text(host, path, state->scratch, sizeof(state->scratch), &state->http_result);
    if (status != NET_OK) {
        char line[160];
        int off = 0;
        line[0] = '\0';
        (void)shell_append_text(line, sizeof(line), &off, "error: HTTP request failed: ");
        (void)shell_append_text(line, sizeof(line), &off, shell_net_error_string(status));
        shell_println(line);
        return -1;
    }
    return shell_print_http_response(arg, state->scratch, &state->http_result);
}

static USER_CODE int shell_run_https(user_shell_state_t* state, const char* arg) {
    char host[96];
    char path[160];
    char line[192];
    int status;

    if (!state) return -1;
    if (!arg || arg[0] == '\0') {
        shell_print_usage("Usage: https https://<host>/<path>");
        return -1;
    }
    if (shell_parse_http_target(arg, host, sizeof(host), path, sizeof(path)) != 0) {
        shell_print_usage("Usage: https https://<host>/<path>");
        return -1;
    }
    status = user_https_fetch_text(host, path, state->scratch, sizeof(state->scratch), &state->http_result);
    if (status != NET_OK) {
        int off = 0;
        line[0] = '\0';
        (void)shell_append_text(line, sizeof(line), &off, "error: HTTPS request failed: ");
        (void)shell_append_text(line, sizeof(line), &off, user_tls_error_string(status));
        shell_println(line);
        line[0] = '\0';
        off = 0;
        (void)shell_append_text(line, sizeof(line), &off, "https stage: ");
        (void)shell_append_text(line, sizeof(line), &off, user_tls_debug_stage_name());
        shell_println(line);
        if (user_tls_debug_detail()[0] != '\0') {
            line[0] = '\0';
            off = 0;
            (void)shell_append_text(line, sizeof(line), &off, "https detail: ");
            (void)shell_append_text(line, sizeof(line), &off, user_tls_debug_detail());
            shell_println(line);
        }
        return -1;
    }
    return shell_print_http_response(arg, state->scratch, &state->http_result);
}

static USER_CODE int shell_run_netdemo(user_shell_state_t* state, const char* arg) {
    char host[96];
    char path[160];
    int status;

    if (!state) return -1;
    if (!arg || arg[0] == '\0') {
        shell_print_usage("Usage: netdemo <host> [path]");
        return -1;
    }
    if (shell_parse_http_target(arg, host, sizeof(host), path, sizeof(path)) != 0) {
        shell_print_usage("Usage: netdemo <host> [path]");
        return -1;
    }

    shell_println(msg_netdemo_start);
    status = user_http_fetch_text(host, path, state->scratch, sizeof(state->scratch), &state->http_result);
    if (status != NET_OK) {
        shell_println(msg_netdemo_err);
        return -1;
    }
    shell_println(msg_netdemo_ok);
    shell_println(state->scratch[0] != '\0' ? state->scratch : msg_empty_response);
    if (state->http_result.truncated != 0U) shell_println(msg_netdemo_truncated);
    if (state->http_result.complete == 0U) shell_println(msg_netdemo_partial);
    return 0;
}

static USER_CODE int shell_run_fetch(user_shell_state_t* state, const char* arg) {
    char host[96];
    char path[160];
    char output_path[128];
    char line[192];
    int status;
    int use_https = 0;
    uint32_t body_offset;
    int off = 0;

    if (!state) return -1;
    if (!arg || arg[0] == '\0') {
        shell_print_usage("Usage: fetch <host> [path] <output-file>");
        shell_print_usage("   or: fetch https://<host>/<path> <output-file>");
        return -1;
    }
    if (shell_parse_fetch_args(arg, host, sizeof(host), path, sizeof(path),
                               output_path, sizeof(output_path), &use_https) != 0) {
        shell_print_usage("Usage: fetch <host> [path] <output-file>");
        shell_print_usage("   or: fetch https://<host>/<path> <output-file>");
        return -1;
    }

    shell_println(msg_fetch_start);
    if (use_https) {
        status = user_https_fetch_text(host, path, state->scratch, sizeof(state->scratch), &state->http_result);
    } else {
        status = user_http_fetch_text(host, path, state->scratch, sizeof(state->scratch), &state->http_result);
    }
    if (status != NET_OK) {
        char err_line[160];
        int err_off = 0;
        err_line[0] = '\0';
        (void)shell_append_text(err_line, sizeof(err_line), &err_off, "fetch: ");
        (void)shell_append_text(err_line, sizeof(err_line), &err_off,
                                use_https ? user_tls_error_string(status) : shell_net_error_string(status));
        shell_println(err_line);
        if (use_https) {
            char tls_line[192];
            int tls_off = 0;
            tls_line[0] = '\0';
            (void)shell_append_text(tls_line, sizeof(tls_line), &tls_off, "fetch https stage: ");
            (void)shell_append_text(tls_line, sizeof(tls_line), &tls_off, user_tls_debug_stage_name());
            shell_println(tls_line);
            if (user_tls_debug_detail()[0] != '\0') {
                tls_line[0] = '\0';
                tls_off = 0;
                (void)shell_append_text(tls_line, sizeof(tls_line), &tls_off, "fetch https detail: ");
                (void)shell_append_text(tls_line, sizeof(tls_line), &tls_off, user_tls_debug_detail());
                shell_println(tls_line);
            }
        }
        return -1;
    }

    body_offset = user_http_find_body(state->scratch, state->http_result.response_len);
    if (user_fs_write(output_path, state->scratch + body_offset) != 0) {
        shell_println(msg_fetch_write_err);
        return -1;
    }

    line[0] = '\0';
    (void)shell_append_text(line, sizeof(line), &off, "Saved ");
    (void)shell_append_uint(line, sizeof(line), &off, (uint32_t)strlen(state->scratch + body_offset));
    (void)shell_append_text(line, sizeof(line), &off, " bytes to ");
    (void)shell_append_text(line, sizeof(line), &off, output_path);
    shell_println(line);
    if (state->http_result.truncated != 0U) shell_println(msg_http_truncated);
    if (state->http_result.complete == 0U) shell_println(msg_http_partial);
    return 0;
}

static USER_CODE void shell_print_tls_summary_line(const char* label, uint32_t value) {
    char line[96];
    int off = 0;

    line[0] = '\0';
    (void)shell_append_text(line, sizeof(line), &off, label);
    (void)shell_append_uint(line, sizeof(line), &off, value);
    shell_println(line);
}

static USER_CODE void shell_print_tls_case(const user_tls_selftest_case_t* test_case) {
    char line[192];
    int off = 0;

    if (!test_case) return;
    line[0] = '\0';
    (void)shell_append_text(line, sizeof(line), &off, msg_tls_test_case_prefix);
    (void)shell_append_text(line, sizeof(line), &off, user_tls_test_status_name(test_case->status));
    (void)shell_append_text(line, sizeof(line), &off, msg_tls_test_case_sep);
    (void)shell_append_text(line, sizeof(line), &off, test_case->name);
    if (test_case->detail[0] != '\0') {
        (void)shell_append_text(line, sizeof(line), &off, msg_tls_test_dash);
        (void)shell_append_text(line, sizeof(line), &off, test_case->detail);
    }
    shell_println(line);
}

static USER_CODE int shell_run_tls_test(user_shell_state_t* state, const char* arg) {
    int status;

    if (!state) return -1;
    if (arg && shell_skip_spaces(arg)[0] != '\0') {
        shell_print_usage("Usage: tls_test");
        return -1;
    }

    status = user_tls_run_selftests(&state->tls_report);
    shell_println(msg_tls_test_header);
    shell_print_tls_summary_line("Total          : ", state->tls_report.total_count);
    shell_print_tls_summary_line("Passed         : ", state->tls_report.pass_count);
    shell_print_tls_summary_line("Failed         : ", state->tls_report.fail_count);
    shell_print_tls_summary_line("Pending        : ", state->tls_report.pending_count);
    shell_print_tls_summary_line("Skipped        : ", state->tls_report.skip_count);

    for (uint32_t i = 0; i < state->tls_report.total_count && i < USER_TLS_SELFTEST_MAX_CASES; i++) {
        shell_print_tls_case(&state->tls_report.cases[i]);
    }
    return status;
}

static USER_CODE int shell_run_edit(const char* arg) {
    char line[192];
    int off = 0;

    if (!arg || arg[0] == '\0') {
        shell_print_usage("Usage: edit <file>");
        return -1;
    }
    if (user_priv_command(PRIV_CMD_EDIT, arg) != 0) {
        shell_print_error("error: Failed to open editor.");
        return -1;
    }
    line[0] = '\0';
    (void)shell_append_text(line, sizeof(line), &off, "Exited NarcVim. ");
    (void)shell_append_text(line, sizeof(line), &off, arg);
    shell_println(line);
    return 0;
}

static USER_CODE int shell_run_privileged(int priv_cmd, const char* arg, const char* error_message) {
    if (user_priv_command(priv_cmd, arg) != 0) {
        shell_print_error(error_message);
        return -1;
    }
    return 0;
}

void USER_CODE user_shell_entry_c(user_shell_state_t* state) {
    char command[32];
    const char* args;
    int status = 0;

    if (!state) return;

    command[0] = '\0';
    if (shell_copy_token(state->command, command, sizeof(command)) != 0) {
        state->exit_code = 0;
        state->status = USER_APP_STATUS_OK;
        return;
    }
    args = shell_find_arg_tail(state->command);

    if (strcmp(command, "help") == 0) shell_run_help();
    else if (strcmp(command, "clear") == 0) user_clear_screen();
    else if (strcmp(command, "mem") == 0) status = shell_run_privileged(PRIV_CMD_MEM, "", "error: Unable to show memory map.");
    else if (strcmp(command, "snake") == 0) status = shell_run_privileged(PRIV_CMD_SNAKE, "", "error: snake requires graphics mode.");
    else if (strcmp(command, "settings") == 0) status = shell_run_privileged(PRIV_CMD_SETTINGS, "", "error: settings requires graphics mode.");
    else if (strcmp(command, "ver") == 0) shell_println("NarcOs");
    else if (strcmp(command, "uptime") == 0) {
        char line[96];
        int off = 0;
        line[0] = '\0';
        (void)shell_append_text(line, sizeof(line), &off, "System Uptime (seconds): ");
        (void)shell_append_uint(line, sizeof(line), &off, user_uptime_ticks() / 100U);
        shell_println(line);
    } else if (strcmp(command, "date") == 0) status = shell_run_local_date(state, 1);
    else if (strcmp(command, "time") == 0) status = shell_run_local_date(state, 0);
    else if (strcmp(command, "ls") == 0) status = shell_run_ls(state);
    else if (strcmp(command, "pwd") == 0) status = shell_run_pwd(state);
    else if (strcmp(command, "touch") == 0) status = shell_run_touch(args);
    else if (strcmp(command, "cat") == 0) status = shell_run_cat(state, args);
    else if (strcmp(command, "write") == 0) status = shell_run_write(args);
    else if (strcmp(command, "edit") == 0) status = shell_run_edit(args);
    else if (strcmp(command, "mkdir") == 0) status = shell_run_mkdir(args);
    else if (strcmp(command, "cd") == 0) status = shell_run_cd(args);
    else if (strcmp(command, "rm") == 0) status = shell_run_rm(args);
    else if (strcmp(command, "mv") == 0) status = shell_run_mv(args);
    else if (strcmp(command, "ren") == 0) status = shell_run_ren(args);
    else if (strcmp(command, "net") == 0) status = shell_run_net_status();
    else if (strcmp(command, "dhcp") == 0) status = shell_run_dhcp();
    else if (strcmp(command, "dns") == 0) status = shell_run_dns(args);
    else if (strcmp(command, "ping") == 0) status = shell_run_ping(state, args);
    else if (strcmp(command, "ntp") == 0) status = shell_run_ntp(state, args);
    else if (strcmp(command, "http") == 0) status = shell_run_http(state, args);
    else if (strcmp(command, "https") == 0) status = shell_run_https(state, args);
    else if (strcmp(command, "netdemo") == 0) status = shell_run_netdemo(state, args);
    else if (strcmp(command, "fetch") == 0) status = shell_run_fetch(state, args);
    else if (strcmp(command, "tls_test") == 0) status = shell_run_tls_test(state, args);
    else if (strcmp(command, "malloc_test") == 0) status = shell_run_privileged(PRIV_CMD_MALLOC_TEST, "", "error: malloc_test failed.");
    else if (strcmp(command, "usermode_test") == 0) status = shell_run_privileged(PRIV_CMD_USERMODE_TEST, "", "error: usermode_test requires graphics mode.");
    else if (strcmp(command, "hwinfo") == 0) status = shell_run_privileged(PRIV_CMD_HWINFO, "", "error: Unable to show hardware info.");
    else if (strcmp(command, "pci") == 0) status = shell_run_privileged(PRIV_CMD_PCI, "", "error: Unable to show PCI devices.");
    else if (strcmp(command, "storage") == 0) status = shell_run_privileged(PRIV_CMD_STORAGE, "", "error: Unable to show storage status.");
    else if (strcmp(command, "log") == 0) status = shell_run_privileged(PRIV_CMD_LOG, "", "error: Unable to show kernel log.");
    else if (strcmp(command, "reboot") == 0) status = shell_run_privileged(PRIV_CMD_REBOOT, "", "error: Reboot failed.");
    else if (strcmp(command, "poweroff") == 0) status = shell_run_privileged(PRIV_CMD_POWEROFF, "", "error: Power off failed.");
    else {
        user_print_raw(msg_unknown_prefix);
        user_print_raw(command);
        shell_println(msg_unknown_suffix);
        status = -1;
    }

    state->exit_code = status;
    state->status = USER_APP_STATUS_OK;
}
