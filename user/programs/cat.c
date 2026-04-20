#include "fs.h"
#include "user_lib.h"

#define CAT_CHUNK_SIZE 256

static int cat_stream_stdin(void) {
    char buffer[CAT_CHUNK_SIZE];

    for (;;) {
        int rc = user_read(USER_STDIN, buffer, sizeof(buffer));
        if (rc < 0) {
            userlib_print_error("cat: stdin read failed");
            return 1;
        }
        if (rc == 0) return 0;
        if (userlib_write_all(USER_STDOUT, buffer, (uint32_t)rc) != 0) return 1;
    }
}

static int cat_file(const char* path) {
    disk_fs_node_t node;
    char buffer[CAT_CHUNK_SIZE];
    uint32_t offset = 0;
    int idx = user_fs_find_node(path);

    if (idx < 0 || user_fs_get_node_info(idx, &node) != 0 || node.flags != FS_NODE_FILE) {
        userlib_print_error("cat: file not found");
        return 1;
    }

    while (offset < node.size) {
        int want = userlib_min_int((int)(node.size - offset), (int)sizeof(buffer));
        int rc = user_fs_read_raw(path, buffer, (uint32_t)want, offset);

        if (rc <= 0) {
            userlib_print_error("cat: file read failed");
            return 1;
        }
        if (userlib_write_all(USER_STDOUT, buffer, (uint32_t)rc) != 0) return 1;
        offset += (uint32_t)rc;
    }
    return 0;
}

int main(int argc, char** argv) {
    int exit_code = 0;

    if (argc == 1) return cat_stream_stdin();
    for (int i = 1; i < argc; i++) {
        if (cat_file(argv[i]) != 0) exit_code = 1;
    }
    return exit_code;
}
