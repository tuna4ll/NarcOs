#include "process_api.h"
#include "user_lib.h"

#define PS_MAX_ENTRIES 16

static const char* ps_state_name(int state) {
    switch (state) {
        case 1: return "runnable";
        case 2: return "running";
        case 3: return "zombie";
        default: return "unknown";
    }
}

static const char* ps_kind_name(int kind) {
    return kind == 1 ? "user" : "kernel";
}

int main(void) {
    process_snapshot_entry_t entries[PS_MAX_ENTRIES];
    int count = user_process_snapshot(entries, PS_MAX_ENTRIES);

    if (count < 0) {
        userlib_print_error("ps: snapshot failed");
        return 1;
    }

    if (userlib_println("PID\tPPID\tSTATE\tKIND\tNAME\tIMAGE") != 0) return 1;
    for (int i = 0; i < count; i++) {
        const char* image = entries[i].image_path[0] != '\0' ? entries[i].image_path : "-";

        if (userlib_print_i32_fd(USER_STDOUT, entries[i].pid) != 0) return 1;
        if (userlib_write_all(USER_STDOUT, "\t", 1U) != 0) return 1;
        if (userlib_print_i32_fd(USER_STDOUT, entries[i].parent_pid) != 0) return 1;
        if (userlib_write_all(USER_STDOUT, "\t", 1U) != 0) return 1;
        if (userlib_print(ps_state_name(entries[i].state)) != 0) return 1;
        if (userlib_write_all(USER_STDOUT, "\t", 1U) != 0) return 1;
        if (userlib_print(ps_kind_name(entries[i].kind)) != 0) return 1;
        if (userlib_write_all(USER_STDOUT, "\t", 1U) != 0) return 1;
        if (userlib_print(entries[i].name) != 0) return 1;
        if (userlib_write_all(USER_STDOUT, "\t", 1U) != 0) return 1;
        if (userlib_println(image) != 0) return 1;
    }

    return 0;
}
