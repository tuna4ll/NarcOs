#include "user_lib.h"

int main(void) {
    return userlib_println("hello from /bin/hello v2") == 0 ? 0 : 1;
}
