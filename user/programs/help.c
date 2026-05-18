#include "user_lib.h"

static const char* help_lines[] = {
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
    "  ps      - List running processes",
    "  procdump - Dump process table to serial log",
    "  proc_test - Run waitpid/zombie self-test",
    "  pipe_test - Run pipe scheduling self-test",
    "  credits - Show project credits",
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

int main(void) {
    uint32_t count = (uint32_t)(sizeof(help_lines) / sizeof(help_lines[0]));

    for (uint32_t i = 0; i < count; i++) {
        if (userlib_println(help_lines[i]) != 0) return 1;
    }
    return 0;
}
