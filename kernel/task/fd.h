#ifndef FD_H
#define FD_H

#include <stdint.h>
#include "pipe.h"
#include "process.h"

#define FD_ACCESS_READ  0x01U
#define FD_ACCESS_WRITE 0x02U

#define FD_OPEN_CREATE 0x01U
#define FD_OPEN_TRUNC  0x02U
#define FD_OPEN_APPEND 0x04U

typedef enum {
    FD_KIND_NONE = 0,
    FD_KIND_CONSOLE,
    FD_KIND_FILE,
    FD_KIND_PIPE
} fd_kind_t;

struct fd_handle {
    fd_kind_t kind;
    uint32_t refs;
    uint32_t access;
    uint32_t flags;
    uint32_t offset;
    union {
        struct {
            int node_idx;
        } file;
        struct {
            pipe_t* pipe;
            uint8_t is_writer;
        } pipe_end;
    } u;
};

int fd_init_process(process_t* proc, process_t* parent);
void fd_cleanup_process(process_t* proc);
void fd_close_from(process_t* proc, int first_fd);
int fd_read(process_t* proc, int fd, void* buffer, uint32_t len);
int fd_write(process_t* proc, int fd, const void* buffer, uint32_t len);
int fd_close(process_t* proc, int fd);
int fd_dup2(process_t* proc, int oldfd, int newfd);
int fd_pipe(process_t* proc, int out_fds[2]);
int fd_open_file(process_t* proc, const char* path, uint32_t access, uint32_t open_flags, int target_fd);

#endif
