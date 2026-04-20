#include "user_lib.h"

int main(int argc, char** argv) {
    int exit_code = 0;

    if (argc < 2) {
        userlib_print_error("Usage: kill <pid> [pid...]");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        int pid = 0;

        if (userlib_parse_i32(argv[i], &pid) != 0 || pid <= 0) {
            userlib_print_error("kill: invalid pid");
            exit_code = 1;
            continue;
        }
        if (user_kill(pid) != 0) {
            userlib_print_error("kill: syscall failed");
            exit_code = 1;
        }
    }

    return exit_code;
}
