#include "process_api.h"
#include "user_lib.h"

#define NEOFETCH_PROCESS_MAX 16
#define NEOFETCH_LOGO_WIDTH 30U
#define NEOFETCH_OUTPUT_MAX 4096U

#define C_RESET "\033[0m"
#define C_DIM "\033[90m"
#define C_USER "\033[1;32m"
#define C_HOST "\033[1;36m"
#define C_KEY "\033[1;34m"
#define C_VALUE "\033[97m"
#define C_LOGO_A "\033[1;36m"
#define C_LOGO_B "\033[1;34m"
#define C_LOGO_C "\033[1;35m"
#define C_RED "\033[31m"
#define C_GREEN "\033[32m"
#define C_YELLOW "\033[33m"
#define C_BLUE "\033[34m"
#define C_MAGENTA "\033[35m"
#define C_CYAN "\033[36m"
#define C_WHITE "\033[37m"
#define C_BRIGHT_RED "\033[91m"
#define C_BRIGHT_GREEN "\033[92m"
#define C_BRIGHT_YELLOW "\033[93m"
#define C_BRIGHT_BLUE "\033[94m"
#define C_BRIGHT_MAGENTA "\033[95m"
#define C_BRIGHT_CYAN "\033[96m"
#define C_BRIGHT_WHITE "\033[97m"

static const char* neofetch_logo[] = {
    "        @%%%%%%%%%%%%",
    "        %%%%%%%%%%%%%%@",
    "        %%%%%%%%%%%%%%%%",
    "        %%%%%%%%%%%%%%%%%%",
    "         %%%%%%%%%%%%%%%%%",
    "                 %%%%%%%%%",
    "                  %%%%%%%%",
    "    %%%%           %%%%%%%",
    "   %%%%%@          %%%%%%%",
    "  %%%%%%%          %%%%%%%",
    " @%%%%%%%%         %%%%%%%",
    "%%%%%%%%%%         %%%%%%%",
    "%%%%%%%%%%         %%%%%%%",
    "%%%%%%%%%%         %%%%%%%",
    "%%%%%%%%%%         %%%%%%%",
    "%%%%%%%%%%         %%%%%%%",
    "%%%%%%%%%%         %%%%%%%",
    "%%%%%%%%%%         %%%%%%%",
    "%%%%%%%%%          %%%%%%%"
};

static char neofetch_output[NEOFETCH_OUTPUT_MAX];
static uint32_t neofetch_output_len = 0;

static int append_bytes(const char* text, uint32_t len) {
    if (!text && len != 0U) return -1;
    if (neofetch_output_len + len > NEOFETCH_OUTPUT_MAX) return -1;
    userlib_memcpy(neofetch_output + neofetch_output_len, text, len);
    neofetch_output_len += len;
    return 0;
}

static int print_text(const char* text) {
    return append_bytes(text, (uint32_t)userlib_strlen(text));
}

static int print_newline(void) {
    return append_bytes("\n", 1U);
}

static int print_uint(uint32_t value) {
    char digits[16];
    uint32_t len = userlib_format_u32(digits, value);

    return append_bytes(digits, len);
}

static int print_int(int value) {
    uint32_t magnitude;

    if (value < 0) {
        if (append_bytes("-", 1U) != 0) return -1;
        magnitude = (uint32_t)(-(value + 1)) + 1U;
        return print_uint(magnitude);
    }
    return print_uint((uint32_t)value);
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

static int print_uptime_value(uint32_t ticks) {
    uint32_t total_seconds = ticks / 100U;
    uint32_t hours = total_seconds / 3600U;
    uint32_t minutes = (total_seconds % 3600U) / 60U;
    uint32_t seconds = total_seconds % 60U;

    if (hours != 0U) {
        if (print_uint(hours) != 0) return -1;
        if (print_text(" hours, ") != 0) return -1;
    }
    if (print_uint(minutes) != 0) return -1;
    if (print_text(" mins, ") != 0) return -1;
    if (print_uint(seconds) != 0) return -1;
    return print_text(" secs");
}

static int print_logo_prefix(const char* logo, const char* color) {
    uint32_t len = (uint32_t)userlib_strlen(logo);

    if (color && print_text(color) != 0) return -1;
    if (print_text(logo) != 0) return -1;
    if (print_text(C_RESET) != 0) return -1;
    while (len < NEOFETCH_LOGO_WIDTH) {
        if (print_text(" ") != 0) return -1;
        len++;
    }
    return print_text("  ");
}

static const char* logo_color_for_line(int line) {
    if (line < 6) return C_LOGO_A;
    if (line < 13) return C_LOGO_B;
    return C_LOGO_C;
}

static int print_logo_only_line(const char* logo, const char* color) {
    if (print_logo_prefix(logo, color) != 0) return -1;
    return print_newline();
}

static int print_header_line(const char* logo, const char* color) {
    if (print_logo_prefix(logo, color) != 0) return -1;
    if (print_text(C_USER "root" C_RESET "@" C_HOST "narcos" C_RESET) != 0) return -1;
    return print_newline();
}

static int print_separator_line(const char* logo, const char* color) {
    if (print_logo_prefix(logo, color) != 0) return -1;
    if (print_text(C_DIM "-----------" C_RESET) != 0) return -1;
    return print_newline();
}

static int print_value_prefix(const char* logo, const char* color, const char* label) {
    if (print_logo_prefix(logo, color) != 0) return -1;
    if (print_text(C_KEY) != 0) return -1;
    if (print_text(label) != 0) return -1;
    return print_text(C_RESET ": " C_VALUE);
}

static int print_value_suffix(void) {
    if (print_text(C_RESET) != 0) return -1;
    return print_newline();
}

static int print_static_line(const char* logo, const char* color,
                             const char* label, const char* value) {
    if (print_value_prefix(logo, color, label) != 0) return -1;
    if (print_text(value) != 0) return -1;
    return print_value_suffix();
}

static int print_arch_line(const char* logo, const char* color) {
#if defined(__x86_64__)
    return print_static_line(logo, color, "Kernel", "NarcOs experimental x86_64");
#else
    return print_static_line(logo, color, "Kernel", "NarcOs experimental i386");
#endif
}

static int print_uptime_line(const char* logo, const char* color, uint32_t ticks) {
    if (print_value_prefix(logo, color, "Uptime") != 0) return -1;
    if (print_uptime_value(ticks) != 0) return -1;
    return print_value_suffix();
}

static int print_pid_line(const char* logo, const char* color) {
    if (print_value_prefix(logo, color, "PID") != 0) return -1;
    if (print_int(user_getpid()) != 0) return -1;
    if (print_text(" (parent ") != 0) return -1;
    if (print_int(user_getppid()) != 0) return -1;
    if (print_text(")") != 0) return -1;
    return print_value_suffix();
}

static int print_process_count_line(const char* logo, const char* color) {
    process_snapshot_entry_t entries[NEOFETCH_PROCESS_MAX];
    int count = user_process_snapshot(entries, NEOFETCH_PROCESS_MAX);

    if (count < 0) return print_static_line(logo, color, "Processes", "unknown");
    if (print_value_prefix(logo, color, "Processes") != 0) return -1;
    if (print_int(count) != 0) return -1;
    return print_value_suffix();
}

static int print_network_line(const char* logo, const char* color) {
    net_ipv4_config_t config;
    int status = user_net_get_config(&config);

    if (status != 0) return print_static_line(logo, color, "Network", "unavailable");
    if (print_value_prefix(logo, color, "Network") != 0) return -1;
    if (!config.available) {
        if (print_text("unavailable") != 0) return -1;
    } else if (!config.configured) {
        if (print_text("ready, no IPv4 lease") != 0) return -1;
    } else {
        if (print_ip(config.ip_addr) != 0) return -1;
        if (print_text(" dhcp") != 0) return -1;
    }
    return print_value_suffix();
}

static int print_cwd_line(const char* logo, const char* color) {
    char cwd[128];

    if (user_fs_get_cwd(cwd, sizeof(cwd)) != 0) {
        return print_static_line(logo, color, "CWD", "unknown");
    }
    if (print_value_prefix(logo, color, "CWD") != 0) return -1;
    if (print_text(cwd) != 0) return -1;
    return print_value_suffix();
}

static int print_time_line(const char* logo, const char* color) {
    rtc_local_time_t now;
    int tz_minutes;

    if (user_get_local_time(&now) != 0) {
        return print_static_line(logo, color, "Time", "unknown");
    }
    tz_minutes = user_get_timezone_offset_minutes();

    if (print_value_prefix(logo, color, "Time") != 0) return -1;
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
    if (print_text(" UTC") != 0) return -1;
    if (tz_minutes >= 0) {
        if (print_text("+") != 0) return -1;
    } else {
        if (print_text("-") != 0) return -1;
        tz_minutes = -tz_minutes;
    }
    if (print_int(tz_minutes / 60) != 0) return -1;
    if (print_text(":") != 0) return -1;
    if (print_two_digits((uint32_t)(tz_minutes % 60)) != 0) return -1;
    return print_value_suffix();
}

static int print_resolution_line(const char* logo, const char* color) {
    gui_screen_info_t info;

    if (user_gui_get_screen_info(&info) != 0 || info.width == 0U || info.height == 0U) {
        return print_static_line(logo, color, "Resolution", "text console");
    }
    if (print_value_prefix(logo, color, "Resolution") != 0) return -1;
    if (print_uint(info.width) != 0) return -1;
    if (print_text("x") != 0) return -1;
    if (print_uint(info.height) != 0) return -1;
    if (print_text(" @ ") != 0) return -1;
    if (print_uint(info.bpp) != 0) return -1;
    if (print_text("bpp") != 0) return -1;
    return print_value_suffix();
}

static int print_color_blocks(const char* logo, const char* color, int bright) {
    if (print_logo_prefix(logo, color) != 0) return -1;
    if (!bright) {
        if (print_text(C_DIM "###" C_RED "###" C_GREEN "###" C_YELLOW "###"
                       C_BLUE "###" C_MAGENTA "###" C_CYAN "###" C_WHITE "###" C_RESET) != 0) return -1;
    } else {
        if (print_text(C_DIM "###" C_BRIGHT_RED "###" C_BRIGHT_GREEN "###" C_BRIGHT_YELLOW "###"
                       C_BRIGHT_BLUE "###" C_BRIGHT_MAGENTA "###" C_BRIGHT_CYAN "###"
                       C_BRIGHT_WHITE "###" C_RESET) != 0) return -1;
    }
    return print_newline();
}

int main(void) {
    uint32_t uptime_ticks = user_uptime_ticks();
    uint32_t logo_count = (uint32_t)(sizeof(neofetch_logo) / sizeof(neofetch_logo[0]));
    uint32_t line = 0;

    if (print_header_line(neofetch_logo[line], logo_color_for_line((int)line)) != 0) return 1;
    line++;
    if (print_separator_line(neofetch_logo[line], logo_color_for_line((int)line)) != 0) return 1;
    line++;
    if (print_static_line(neofetch_logo[line], logo_color_for_line((int)line), "OS", "NarcOs") != 0) return 1;
    line++;
    if (print_static_line(neofetch_logo[line], logo_color_for_line((int)line), "Host", "PC compatible") != 0) return 1;
    line++;
    if (print_arch_line(neofetch_logo[line], logo_color_for_line((int)line)) != 0) return 1;
    line++;
    if (print_uptime_line(neofetch_logo[line], logo_color_for_line((int)line), uptime_ticks) != 0) return 1;
    line++;
    if (print_static_line(neofetch_logo[line], logo_color_for_line((int)line), "Packages", "12 (/bin)") != 0) return 1;
    line++;
    if (print_static_line(neofetch_logo[line], logo_color_for_line((int)line), "Shell", "narcsh") != 0) return 1;
    line++;
    if (print_resolution_line(neofetch_logo[line], logo_color_for_line((int)line)) != 0) return 1;
    line++;
    if (print_static_line(neofetch_logo[line], logo_color_for_line((int)line), "DE", "Narc Desktop") != 0) return 1;
    line++;
    if (print_static_line(neofetch_logo[line], logo_color_for_line((int)line), "WM", "NarcWM compositor") != 0) return 1;
    line++;
    if (print_static_line(neofetch_logo[line], logo_color_for_line((int)line), "Terminal", "NarcOs Terminal") != 0) return 1;
    line++;
#if defined(__x86_64__)
    if (print_static_line(neofetch_logo[line], logo_color_for_line((int)line), "CPU", "x86_64 CPUID") != 0) return 1;
#else
    if (print_static_line(neofetch_logo[line], logo_color_for_line((int)line), "CPU", "i386 CPUID") != 0) return 1;
#endif
    line++;
    if (print_static_line(neofetch_logo[line], logo_color_for_line((int)line), "GPU", "VBE framebuffer") != 0) return 1;
    line++;
    if (print_pid_line(neofetch_logo[line], logo_color_for_line((int)line)) != 0) return 1;
    line++;
    if (print_process_count_line(neofetch_logo[line], logo_color_for_line((int)line)) != 0) return 1;
    line++;
    if (print_network_line(neofetch_logo[line], logo_color_for_line((int)line)) != 0) return 1;
    line++;
    if (print_cwd_line(neofetch_logo[line], logo_color_for_line((int)line)) != 0) return 1;
    line++;
    if (print_time_line(neofetch_logo[line], logo_color_for_line((int)line)) != 0) return 1;
    line++;
    while (line < logo_count) {
        if (print_logo_only_line(neofetch_logo[line], logo_color_for_line((int)line)) != 0) return 1;
        line++;
    }
    if (print_logo_prefix("", 0) != 0 || print_newline() != 0) return 1;
    if (print_color_blocks("", 0, 0) != 0) return 1;
    if (print_color_blocks("", 0, 1) != 0) return 1;

    return userlib_write_all(USER_STDOUT, neofetch_output, neofetch_output_len) == 0 ? 0 : 1;
}
