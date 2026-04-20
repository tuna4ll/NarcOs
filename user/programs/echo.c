#include "user_lib.h"

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1 && userlib_write_all(USER_STDOUT, " ", 1U) != 0) return 1;
        if (userlib_print(argv[i]) != 0) return 1;
    }
    if (userlib_write_all(USER_STDOUT, "\n", 1U) != 0) return 1;
    return 0;
}
