#include "net_http_common.h"

static const char* core_tools_basename(const char* path) {
    const char* base = path;

    if (!path) return "";
    while (*path) {
        if (*path == '/') base = path + 1;
        path++;
    }
    return base;
}

static int core_help_main(void) {
    static const char* help_lines[] = {
        "NarcOs Shell",
        "  help    - Show this menu",
        "  clear   - Clear the screen",
        "  mem     - Memory map",
        "  snake   - Snake game (requires graphics mode)",
        "  doom    - Doom port shell (requires graphics mode)",
        "  settings - Open settings (requires graphics mode)",
        "  ver     - Show version",
        "  uptime  - Show system uptime in seconds",
        "  date    - Show current local date",
        "  time    - Show current local time",
        "  ls      - List files",
        "  pwd     - Show current path",
        "  ps      - List running processes",
        "  procdump - Dump process table to serial log",
        "  proc_test - Run waitpid/zombie self-test",
        "  pipe_test - Run pipe scheduling self-test",
        "  echo    - Print arguments",
        "  spawn   - Launch an external process",
        "  wait    - Wait for a child process",
        "  kill    - Terminate a process",
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
        "  https   - Fetch HTTPS response (https https://pinned-host/path)",
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
    uint32_t count = (uint32_t)(sizeof(help_lines) / sizeof(help_lines[0]));

    for (uint32_t i = 0; i < count; i++) {
        if (userlib_println(help_lines[i]) != 0) return 1;
    }
    return 0;
}

static int core_clear_main(void) {
    user_clear_screen();
    return 0;
}

static int core_ver_main(void) {
    return userlib_println("NarcOs") == 0 ? 0 : 1;
}

static int core_uptime_main(void) {
    char line[96];
    int off = 0;

    line[0] = '\0';
    if (prog_append_text(line, sizeof(line), &off, "System Uptime (seconds): ") != 0) return 1;
    if (prog_append_uint(line, sizeof(line), &off, user_uptime_ticks() / 100U) != 0) return 1;
    return userlib_println(line) == 0 ? 0 : 1;
}

static int core_date_main(int date_only) {
    rtc_local_time_t now;
    char datetime[64];
    char line[96];
    int off = 0;

    if (user_get_local_time(&now) != 0) {
        userlib_print_error(date_only ? "date: failed to read RTC" : "time: failed to read RTC");
        return 1;
    }
    if (prog_format_datetime(datetime, sizeof(datetime), &now, date_only) != 0) return 1;
    line[0] = '\0';
    if (prog_append_text(line, sizeof(line), &off,
                         date_only ? "Current Local Date: " : "Current Local Time: ") != 0) {
        return 1;
    }
    if (prog_append_text(line, sizeof(line), &off, date_only ? datetime : datetime + 11) != 0) return 1;
    return userlib_println(line) == 0 ? 0 : 1;
}

static int core_ls_main(void) {
    disk_fs_node_t entries[MAX_FILES];
    int count = user_fs_list(entries, MAX_FILES);

    if (count < 0) {
        userlib_print_error("ls: failed to read directory");
        return 1;
    }

    if (userlib_println("Name\t\tSize (Bytes)") != 0) return 1;
    if (userlib_println("----------------------------") != 0) return 1;
    for (int i = 0; i < count; i++) {
        char line[96];
        int off = 0;

        line[0] = '\0';
        if (prog_append_text(line, sizeof(line), &off, entries[i].name) != 0) return 1;
        if (entries[i].flags == FS_NODE_DIR) {
            if (prog_append_text(line, sizeof(line), &off, "/\t\t<DIR>") != 0) return 1;
        } else {
            if (prog_append_char(line, sizeof(line), &off, '\t') != 0) return 1;
            if (userlib_strlen(entries[i].name) < 8U &&
                prog_append_char(line, sizeof(line), &off, '\t') != 0) {
                return 1;
            }
            if (prog_append_uint(line, sizeof(line), &off, entries[i].size) != 0) return 1;
        }
        if (userlib_println(line) != 0) return 1;
    }
    return 0;
}

static int core_pwd_main(void) {
    char path[128];

    if (user_fs_get_cwd(path, sizeof(path)) != 0) {
        userlib_print_error("pwd: failed to get current path");
        return 1;
    }
    return userlib_println(path) == 0 ? 0 : 1;
}

static int core_net_main(void) {
    net_ipv4_config_t config;

    userlib_memset(&config, 0, sizeof(config));
    if (user_net_get_config(&config) != 0 || !config.available) {
        userlib_print_error("Network: driver not ready.");
        return 1;
    }

    if (userlib_println("Network Driver : RTL8139") != 0) return 1;
    if (userlib_println(config.configured ? "Address Mode   : DHCP" : "Address Mode   : Unconfigured") != 0) return 1;
    if (config.configured) {
        if (prog_print_ip_line("IP             : ", config.ip_addr) != 0) return 1;
        if (prog_print_ip_line("Netmask        : ", config.netmask) != 0) return 1;
        if (prog_print_ip_line("Gateway        : ", config.gateway) != 0) return 1;
        if (prog_print_ip_line("DNS            : ", config.dns_server) != 0) return 1;
    }
    return 0;
}

static int core_dns_main(int argc, char** argv) {
    uint32_t ip_addr = 0;
    char line[160];
    int off = 0;

    if (argc != 2) {
        userlib_print_error("Usage: dns <host>");
        return 1;
    }
    if (user_net_resolve(argv[1], &ip_addr) != NET_OK) {
        userlib_print_error("dns: lookup failed");
        return 1;
    }

    line[0] = '\0';
    if (prog_append_text(line, sizeof(line), &off, argv[1]) != 0) return 1;
    if (prog_append_text(line, sizeof(line), &off, " -> ") != 0) return 1;
    if (prog_append_ip(line, sizeof(line), &off, ip_addr) != 0) return 1;
    return userlib_println(line) == 0 ? 0 : 1;
}

static int core_ping_main(int argc, char** argv) {
    net_ping_result_t result;
    char line[160];
    int status;

    if (argc != 2) {
        userlib_print_error("Usage: ping <host>");
        return 1;
    }

    status = user_net_ping(argv[1], &result);
    if (status == NET_ERR_NOT_READY) {
        userlib_print_error("ping: network driver is not ready");
        return 1;
    }
    if (status == NET_ERR_RESOLVE) {
        userlib_print_error("ping: failed to resolve target host");
        return 1;
    }
    if (status == NET_ERR_IO) {
        userlib_print_error("ping: failed to transmit ICMP packet");
        return 1;
    }

    line[0] = '\0';
    {
        int off = 0;
        if (prog_append_text(line, sizeof(line), &off, "Pinging ") != 0) return 1;
        if (prog_append_text(line, sizeof(line), &off, argv[1]) != 0) return 1;
        if (prog_append_text(line, sizeof(line), &off, " [") != 0) return 1;
        if (prog_append_ip(line, sizeof(line), &off, result.resolved_ip) != 0) return 1;
        if (prog_append_text(line, sizeof(line), &off, "] ...") != 0) return 1;
    }
    if (userlib_println(line) != 0) return 1;

    for (uint32_t i = 0; i < result.attempts; i++) {
        if (result.reply_status[i] == NET_OK) {
            int off = 0;

            line[0] = '\0';
            if (prog_append_text(line, sizeof(line), &off, "Reply from ") != 0) return 1;
            if (prog_append_ip(line, sizeof(line), &off, result.resolved_ip) != 0) return 1;
            if (prog_append_text(line, sizeof(line), &off, ": time=") != 0) return 1;
            if (prog_append_uint(line, sizeof(line), &off, result.rtt_ms[i]) != 0) return 1;
            if (prog_append_text(line, sizeof(line), &off, "ms") != 0) return 1;
            if (userlib_println(line) != 0) return 1;
        } else if (userlib_println("Request timed out.") != 0) {
            return 1;
        }
    }
    return status == NET_OK ? 0 : 1;
}

static int core_http_main(int argc, char** argv) {
    char args[192];
    char host[96];
    char path[160];
    char response[4096];
    net_http_result_t result;
    int status;

    if (argc < 2 || prog_join_args(argc, argv, 1, args, sizeof(args)) != 0) {
        userlib_print_error("Usage: http <host> [path]");
        return 1;
    }
    if (prog_parse_http_target(args, host, sizeof(host), path, sizeof(path)) != 0) {
        userlib_print_error("Usage: http <host> [path]");
        return 1;
    }

    status = prog_http_fetch_text(host, path, response, sizeof(response), &result);
    if (status != NET_OK) {
        char line[160];
        int off = 0;

        line[0] = '\0';
        if (prog_append_text(line, sizeof(line), &off, "http: request failed: ") != 0) return 1;
        if (prog_append_text(line, sizeof(line), &off, prog_net_error_string(status)) != 0) return 1;
        userlib_print_error(line);
        return 1;
    }
    return prog_print_http_response(args, response, &result) == 0 ? 0 : 1;
}

static int core_netdemo_main(int argc, char** argv) {
    char args[192];
    char host[96];
    char path[160];
    char response[2048];
    net_http_result_t result;
    int status;

    if (argc < 2 || prog_join_args(argc, argv, 1, args, sizeof(args)) != 0) {
        userlib_print_error("Usage: netdemo <host> [path]");
        return 1;
    }
    if (prog_parse_http_target(args, host, sizeof(host), path, sizeof(path)) != 0) {
        userlib_print_error("Usage: netdemo <host> [path]");
        return 1;
    }

    if (userlib_println("Ring3 netdemo: HTTP request starting") != 0) return 1;
    status = prog_http_fetch_text(host, path, response, sizeof(response), &result);
    if (status != NET_OK) {
        userlib_print_error("Ring3 netdemo: HTTP request failed");
        return 1;
    }
    if (userlib_println("Ring3 netdemo: response") != 0) return 1;
    if (userlib_println(response[0] != '\0' ? response : "(empty response)") != 0) return 1;
    if (result.truncated != 0U && userlib_println("Ring3 netdemo: response truncated") != 0) return 1;
    if (result.complete == 0U &&
        userlib_println("Ring3 netdemo: connection timed out before clean close") != 0) {
        return 1;
    }
    return 0;
}

static int core_tools_dispatch(const char* command, int argc, char** argv) {
    if (userlib_strcmp(command, "help") == 0) return core_help_main();
    if (userlib_strcmp(command, "clear") == 0) return core_clear_main();
    if (userlib_strcmp(command, "ver") == 0) return core_ver_main();
    if (userlib_strcmp(command, "uptime") == 0) return core_uptime_main();
    if (userlib_strcmp(command, "date") == 0) return core_date_main(1);
    if (userlib_strcmp(command, "time") == 0) return core_date_main(0);
    if (userlib_strcmp(command, "ls") == 0) return core_ls_main();
    if (userlib_strcmp(command, "pwd") == 0) return core_pwd_main();
    if (userlib_strcmp(command, "net") == 0) return core_net_main();
    if (userlib_strcmp(command, "dns") == 0) return core_dns_main(argc, argv);
    if (userlib_strcmp(command, "ping") == 0) return core_ping_main(argc, argv);
    if (userlib_strcmp(command, "http") == 0) return core_http_main(argc, argv);
    if (userlib_strcmp(command, "netdemo") == 0) return core_netdemo_main(argc, argv);
    return -1;
}

int main(int argc, char** argv) {
    const char* command = argc > 0 ? core_tools_basename(argv[0]) : "";
    int status = core_tools_dispatch(command, argc, argv);

    if (status >= 0) return status;
    if (argc >= 2) {
        status = core_tools_dispatch(argv[1], argc - 1, argv + 1);
        if (status >= 0) return status;
    }
    userlib_print_error("core_tools: unknown command");
    return 1;
}
