#include "process_api.h"
#include "user_lib.h"

#define NEOFETCH_PROCESS_MAX 16

static int print_text(const char* text) {
    return userlib_print(text);
}

static int print_newline(void) {
    return userlib_write_all(USER_STDOUT, "\n", 1U);
}

static int print_label(const char* label) {
    if (print_text("  ") != 0) return -1;
    if (print_text(label) != 0) return -1;
    return print_text(": ");
}

static int print_uint(uint32_t value) {
    return userlib_print_u32_fd(USER_STDOUT, value);
}

static int print_int(int value) {
    return userlib_print_i32_fd(USER_STDOUT, value);
}

static int print_ip(uint32_t ip_addr) {
    for (int i = 0; i < 4; i++) {
        uint32_t octet = (ip_addr >> (24 - i * 8)) & 0xFFU;

        if (print_uint(octet) != 0) return -1;
        if (i != 3 && print_text(".") != 0) return -1;
    }
    return 0;
}

static int print_two_digits(uint32_t value) {
    if (value < 10U && print_text("0") != 0) return -1;
    return print_uint(value);
}

static int print_uptime(uint32_t ticks) {
    uint32_t total_seconds = ticks / 100U;
    uint32_t hours = total_seconds / 3600U;
    uint32_t minutes = (total_seconds % 3600U) / 60U;
    uint32_t seconds = total_seconds % 60U;

    if (print_uint(hours) != 0) return -1;
    if (print_text("h ") != 0) return -1;
    if (print_uint(minutes) != 0) return -1;
    if (print_text("m ") != 0) return -1;
    if (print_uint(seconds) != 0) return -1;
    return print_text("s");
}

static int print_time_line(void) {
    rtc_local_time_t now;
    int tz_minutes;

    if (user_get_local_time(&now) != 0) return 0;
    tz_minutes = user_get_timezone_offset_minutes();

    if (print_label("Time") != 0) return -1;
    if (print_text("20") != 0) return -1;
    if (print_two_digits(now.year) != 0) return -1;
    if (print_text("-") != 0) return -1;
    if (print_two_digits(now.month) != 0) return -1;
    if (print_text("-") != 0) return -1;
    if (print_two_digits(now.day) != 0) return -1;
    if (print_text(" ") != 0) return -1;
    if (print_two_digits(now.hour) != 0) return -1;
    if (print_text(":") != 0) return -1;
    if (print_two_digits(now.minute) != 0) return -1;
    if (print_text(":") != 0) return -1;
    if (print_two_digits(now.second) != 0) return -1;
    if (print_text(" (UTC") != 0) return -1;
    if (tz_minutes >= 0) {
        if (print_text("+") != 0) return -1;
    } else {
        if (print_text("-") != 0) return -1;
        tz_minutes = -tz_minutes;
    }
    if (print_int(tz_minutes / 60) != 0) return -1;
    if (print_text(":") != 0) return -1;
    if (print_two_digits((uint32_t)(tz_minutes % 60)) != 0) return -1;
    if (print_text(")") != 0) return -1;
    return print_newline();
}

static int print_process_count_line(void) {
    process_snapshot_entry_t entries[NEOFETCH_PROCESS_MAX];
    int count = user_process_snapshot(entries, NEOFETCH_PROCESS_MAX);

    if (count < 0) return 0;
    if (print_label("Processes") != 0) return -1;
    if (print_int(count) != 0) return -1;
    return print_newline();
}

static int print_network_line(void) {
    net_ipv4_config_t config;
    int status = user_net_get_config(&config);

    if (status != 0) return 0;
    if (print_label("Network") != 0) return -1;
    if (!config.available) {
        if (print_text("unavailable") != 0) return -1;
        return print_newline();
    }
    if (!config.configured) {
        if (print_text("driver ready, no IPv4 lease") != 0) return -1;
        return print_newline();
    }
    if (print_ip(config.ip_addr) != 0) return -1;
    if (print_text("  gw ") != 0) return -1;
    if (print_ip(config.gateway) != 0) return -1;
    if (print_text("  dns ") != 0) return -1;
    if (print_ip(config.dns_server) != 0) return -1;
    return print_newline();
}

static int print_cwd_line(void) {
    char cwd[128];

    if (user_fs_get_cwd(cwd, sizeof(cwd)) != 0) return 0;
    if (print_label("CWD") != 0) return -1;
    if (print_text(cwd) != 0) return -1;
    return print_newline();
}

int main(void) {
    uint32_t uptime_ticks = user_uptime_ticks();

 if (userlib_println("      _   __                      ____   _____") != 0) return 1;
if (userlib_println("     / | / /___ ___________     / __ \\ / ___/") != 0) return 1;
if (userlib_println("    /  |/ / __ `/ ___/ ___/    / / / / \\__ \\ ") != 0) return 1;
if (userlib_println("   / /|  / /_/ / /  / /__     / /_/ / ___/ / ") != 0) return 1;
if (userlib_println("  /_/ |_|\\__,_/_/   \\___/     \\____/ /____/  ") != 0) return 1;
if (print_newline() != 0) return 1;

    if (print_label("OS") != 0 || print_text("NarcOs") != 0 || print_newline() != 0) return 1;
    if (print_label("Kernel") != 0 || print_text("NarcOs experimental") != 0 || print_newline() != 0) return 1;
#if defined(__x86_64__)
    if (print_label("Arch") != 0 || print_text("x86_64") != 0 || print_newline() != 0) return 1;
#else
    if (print_label("Arch") != 0 || print_text("i386") != 0 || print_newline() != 0) return 1;
#endif
    if (print_label("Uptime") != 0 || print_uptime(uptime_ticks) != 0 || print_newline() != 0) return 1;
    if (print_label("PID") != 0 || print_int(user_getpid()) != 0 || print_text(" (ppid ") != 0 ||
        print_int(user_getppid()) != 0 || print_text(")") != 0 || print_newline() != 0) return 1;
    if (print_cwd_line() != 0) return 1;
    if (print_process_count_line() != 0) return 1;
    if (print_network_line() != 0) return 1;
    if (print_time_line() != 0) return 1;

    return 0;
}
