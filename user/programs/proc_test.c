#include "user_lib.h"

static int proc_test_child_main(void) {
    (void)user_sleep(10U);
    return 42;
}

int main(int argc, char** argv) {
    static const char* child_argv[] = { "proc_test", "child", 0 };
    int pid;
    int status = 0;
    int rc;

    if (argc > 1 && userlib_strcmp(argv[1], "child") == 0) {
        return proc_test_child_main();
    }

    pid = user_spawn("/bin/proc_test", child_argv, 2U);
    if (pid < 0) {
        userlib_print_error("proc_test: spawn failed");
        return 1;
    }

    rc = user_waitpid(pid, &status, WAITPID_FLAG_NOHANG);
    if (rc != 0) {
        userlib_print_error("proc_test: nohang failed");
        return 1;
    }

    rc = user_waitpid(pid, &status, 0U);
    if (rc != pid || status != 42) {
        userlib_print_error("proc_test: wait returned wrong status");
        return 1;
    }

    rc = user_waitpid(pid, &status, WAITPID_FLAG_NOHANG);
    if (rc != -1) {
        userlib_print_error("proc_test: zombie reap check failed");
        return 1;
    }

    return userlib_println("proc_test: ok") == 0 ? 0 : 1;
}
