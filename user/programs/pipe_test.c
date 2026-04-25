#include "user_lib.h"

#define PIPE_TEST_TOTAL_BYTES 1097
#define PIPE_TEST_CHUNK_BYTES 128

static void pipe_test_u32_to_text(uint32_t value, char* out, int out_len) {
    char digits[16];
    int count = 0;
    int off = 0;

    if (!out || out_len <= 0) return;
    if (value == 0U) {
        if (out_len > 1) {
            out[0] = '0';
            out[1] = '\0';
        } else {
            out[0] = '\0';
        }
        return;
    }

    while (value != 0U && count < (int)sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (count > 0 && off + 1 < out_len) {
        out[off++] = digits[--count];
    }
    out[off] = '\0';
}

static int pipe_test_writer_main(int argc, char** argv) {
    char buffer[PIPE_TEST_CHUNK_BYTES];
    int read_fd = 0;
    int write_fd = 0;
    int target = 0;
    int written = 0;

    if (argc < 5 ||
        userlib_parse_i32(argv[2], &read_fd) != 0 ||
        userlib_parse_i32(argv[3], &write_fd) != 0 ||
        userlib_parse_i32(argv[4], &target) != 0) {
        return 2;
    }

    for (int i = 0; i < (int)sizeof(buffer); i++) buffer[i] = (char)('A' + (i % 26));
    (void)user_close(read_fd);
    while (written < target) {
        int want = userlib_min_int(target - written, (int)sizeof(buffer));
        int rc = user_write(write_fd, buffer, (uint32_t)want);

        if (rc <= 0) return 3;
        written += rc;
    }
    return user_close(write_fd) == 0 ? 0 : 4;
}

static int pipe_test_reader_main(int argc, char** argv) {
    char buffer[PIPE_TEST_CHUNK_BYTES];
    int read_fd = 0;
    int write_fd = 0;
    int target = 0;
    int total = 0;

    if (argc < 5 ||
        userlib_parse_i32(argv[2], &read_fd) != 0 ||
        userlib_parse_i32(argv[3], &write_fd) != 0 ||
        userlib_parse_i32(argv[4], &target) != 0) {
        return 2;
    }

    (void)user_close(write_fd);
    for (;;) {
        int rc = user_read(read_fd, buffer, sizeof(buffer));

        if (rc < 0) return 3;
        if (rc == 0) break;
        total += rc;
    }
    if (user_close(read_fd) != 0) return 4;
    return total == target ? 0 : 5;
}

int main(int argc, char** argv) {
    int pipefd[2];
    char read_text[16];
    char write_text[16];
    char count_text[16];
    int writer_pid;
    int reader_pid;
    int status = 0;
    int rc;
    const char* writer_argv[5];
    const char* reader_argv[5];

    if (argc > 1 && userlib_strcmp(argv[1], "writer") == 0) return pipe_test_writer_main(argc, argv);
    if (argc > 1 && userlib_strcmp(argv[1], "reader") == 0) return pipe_test_reader_main(argc, argv);

    if (user_pipe(pipefd) != 0) {
        userlib_print_error("pipe_test: pipe failed");
        return 1;
    }

    pipe_test_u32_to_text((uint32_t)pipefd[0], read_text, sizeof(read_text));
    pipe_test_u32_to_text((uint32_t)pipefd[1], write_text, sizeof(write_text));
    pipe_test_u32_to_text((uint32_t)PIPE_TEST_TOTAL_BYTES, count_text, sizeof(count_text));

    writer_argv[0] = "pipe_test";
    writer_argv[1] = "writer";
    writer_argv[2] = read_text;
    writer_argv[3] = write_text;
    writer_argv[4] = count_text;
    reader_argv[0] = "pipe_test";
    reader_argv[1] = "reader";
    reader_argv[2] = read_text;
    reader_argv[3] = write_text;
    reader_argv[4] = count_text;

    writer_pid = user_spawn("/bin/pipe_test", writer_argv, 5U);
    if (writer_pid < 0) {
        userlib_print_error("pipe_test: writer spawn failed");
        return 1;
    }

    reader_pid = user_spawn("/bin/pipe_test", reader_argv, 5U);
    if (reader_pid < 0) {
        userlib_print_error("pipe_test: reader spawn failed");
        return 1;
    }

    (void)user_close(pipefd[0]);
    (void)user_close(pipefd[1]);

    rc = user_waitpid(writer_pid, &status, 0U);
    if (rc != writer_pid || status != 0) {
        userlib_print_error("pipe_test: writer failed");
        return 1;
    }

    rc = user_waitpid(reader_pid, &status, 0U);
    if (rc != reader_pid || status != 0) {
        userlib_print_error("pipe_test: reader failed");
        return 1;
    }

    return userlib_println("pipe_test: ok") == 0 ? 0 : 1;
}
