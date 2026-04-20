#include "fd.h"
#include "fs.h"
#include "memory_alloc.h"
#include "string.h"

extern void vga_putchar(char c);
extern int console_input_read(char* buffer, uint32_t max_len);

static fd_handle_t* fd_create_console_handle(uint32_t access) {
    fd_handle_t* handle = (fd_handle_t*)malloc(sizeof(fd_handle_t));

    if (!handle) return 0;
    memset(handle, 0, sizeof(*handle));
    handle->kind = FD_KIND_CONSOLE;
    handle->refs = 1U;
    handle->access = access;
    return handle;
}

static fd_handle_t* fd_create_file_handle(int node_idx, uint32_t access, uint32_t open_flags) {
    disk_fs_node_t node;
    fd_handle_t* handle;

    if (node_idx < 0 || fs_get_node_info(node_idx, &node) != 0 || node.flags != FS_NODE_FILE) return 0;
    handle = (fd_handle_t*)malloc(sizeof(fd_handle_t));
    if (!handle) return 0;
    memset(handle, 0, sizeof(*handle));
    handle->kind = FD_KIND_FILE;
    handle->refs = 1U;
    handle->access = access;
    handle->flags = open_flags;
    handle->offset = (open_flags & FD_OPEN_APPEND) != 0U ? node.size : 0U;
    handle->u.file.node_idx = node_idx;
    return handle;
}

static fd_handle_t* fd_create_pipe_handle(pipe_t* pipe, uint8_t is_writer) {
    fd_handle_t* handle;

    if (!pipe) return 0;
    handle = (fd_handle_t*)malloc(sizeof(fd_handle_t));
    if (!handle) return 0;
    memset(handle, 0, sizeof(*handle));
    handle->kind = FD_KIND_PIPE;
    handle->refs = 1U;
    handle->access = is_writer ? FD_ACCESS_WRITE : FD_ACCESS_READ;
    handle->u.pipe_end.pipe = pipe;
    handle->u.pipe_end.is_writer = is_writer;
    if (is_writer) pipe_acquire_writer(pipe);
    else pipe_acquire_reader(pipe);
    return handle;
}

static void fd_handle_acquire(fd_handle_t* handle) {
    if (!handle) return;
    handle->refs++;
}

static void fd_handle_release(fd_handle_t* handle) {
    if (!handle || handle->refs == 0U) return;
    handle->refs--;
    if (handle->refs != 0U) return;

    if (handle->kind == FD_KIND_PIPE && handle->u.pipe_end.pipe) {
        if (handle->u.pipe_end.is_writer) pipe_release_writer(handle->u.pipe_end.pipe);
        else pipe_release_reader(handle->u.pipe_end.pipe);
    }
    free(handle);
}

static int fd_slot_valid(int fd) {
    return fd >= 0 && fd < PROCESS_MAX_FDS;
}

static int fd_find_free_slot(process_t* proc, int start_fd) {
    int begin = start_fd < 0 ? 0 : start_fd;

    if (!proc) return -1;
    for (int fd = begin; fd < PROCESS_MAX_FDS; fd++) {
        if (!proc->fd_table[fd]) return fd;
    }
    return -1;
}

static int fd_install_handle(process_t* proc, fd_handle_t* handle, int target_fd) {
    int fd;

    if (!proc || !handle) return -1;
    fd = target_fd >= 0 ? target_fd : fd_find_free_slot(proc, 0);
    if (!fd_slot_valid(fd)) return -1;
    if (proc->fd_table[fd]) fd_close(proc, fd);
    proc->fd_table[fd] = handle;
    return fd;
}

static int fd_should_abort_wait(process_t* proc) {
    if (!proc) return 1;
    return proc->killed != 0U || (proc->flags & PROCESS_FLAG_USER_EXIT_PENDING) != 0U;
}

int fd_init_process(process_t* proc, process_t* parent) {
    fd_handle_t* stdin_handle;
    fd_handle_t* stdout_handle;
    fd_handle_t* stderr_handle;

    if (!proc) return -1;
    memset(proc->fd_table, 0, sizeof(proc->fd_table));

    if (parent) {
        for (int fd = 0; fd < PROCESS_MAX_FDS; fd++) {
            proc->fd_table[fd] = parent->fd_table[fd];
            if (proc->fd_table[fd]) fd_handle_acquire(proc->fd_table[fd]);
        }
        return 0;
    }

    stdin_handle = fd_create_console_handle(FD_ACCESS_READ);
    stdout_handle = fd_create_console_handle(FD_ACCESS_WRITE);
    stderr_handle = fd_create_console_handle(FD_ACCESS_WRITE);
    if (!stdin_handle || !stdout_handle || !stderr_handle) {
        fd_handle_release(stdin_handle);
        fd_handle_release(stdout_handle);
        fd_handle_release(stderr_handle);
        return -1;
    }

    proc->fd_table[0] = stdin_handle;
    proc->fd_table[1] = stdout_handle;
    proc->fd_table[2] = stderr_handle;
    return 0;
}

void fd_cleanup_process(process_t* proc) {
    if (!proc) return;
    for (int fd = 0; fd < PROCESS_MAX_FDS; fd++) {
        if (!proc->fd_table[fd]) continue;
        fd_handle_release(proc->fd_table[fd]);
        proc->fd_table[fd] = 0;
    }
}

void fd_close_from(process_t* proc, int first_fd) {
    if (!proc) return;
    if (first_fd < 0) first_fd = 0;
    for (int fd = first_fd; fd < PROCESS_MAX_FDS; fd++) {
        if (!proc->fd_table[fd]) continue;
        (void)fd_close(proc, fd);
    }
}

int fd_read(process_t* proc, int fd, void* buffer, uint32_t len) {
    fd_handle_t* handle;

    if (!proc || !fd_slot_valid(fd) || (!buffer && len != 0U)) return -1;
    if (len == 0U) return 0;
    handle = proc->fd_table[fd];
    if (!handle || (handle->access & FD_ACCESS_READ) == 0U) return -1;

    if (handle->kind == FD_KIND_CONSOLE) {
        int status;

        for (;;) {
            status = console_input_read((char*)buffer, len);
            if (status != 0) return status;
            if (fd_should_abort_wait(proc)) return -1;
            process_yield();
        }
    }

    if (handle->kind == FD_KIND_FILE) {
        int status = fs_read_file_raw_by_idx(handle->u.file.node_idx, buffer, handle->offset, len);
        if (status > 0) handle->offset += (uint32_t)status;
        return status;
    }

    if (handle->kind == FD_KIND_PIPE) {
        pipe_t* pipe = handle->u.pipe_end.pipe;
        int total = 0;

        if (!pipe || handle->u.pipe_end.is_writer != 0U) return -1;
        for (;;) {
            int chunk = pipe_read_some(pipe, (uint8_t*)buffer + total, len - (uint32_t)total);

            if (chunk < 0) return -1;
            total += chunk;
            if ((uint32_t)total == len) return total;
            if (total != 0) return total;
            if (pipe_writer_count(pipe) == 0U) return 0;
            if (fd_should_abort_wait(proc)) return -1;
            process_yield();
        }
    }

    return -1;
}

int fd_write(process_t* proc, int fd, const void* buffer, uint32_t len) {
    fd_handle_t* handle;

    if (!proc || !fd_slot_valid(fd) || (!buffer && len != 0U)) return -1;
    if (len == 0U) return 0;
    handle = proc->fd_table[fd];
    if (!handle || (handle->access & FD_ACCESS_WRITE) == 0U) return -1;

    if (handle->kind == FD_KIND_CONSOLE) {
        const uint8_t* bytes = (const uint8_t*)buffer;

        for (uint32_t i = 0; i < len; i++) vga_putchar((char)bytes[i]);
        return (int)len;
    }

    if (handle->kind == FD_KIND_FILE) {
        disk_fs_node_t node;

        if ((handle->flags & FD_OPEN_APPEND) != 0U &&
            fs_get_node_info(handle->u.file.node_idx, &node) == 0 && node.flags == FS_NODE_FILE) {
            handle->offset = node.size;
        }

        {
            int status = fs_write_file_raw_at_by_idx(handle->u.file.node_idx, buffer, handle->offset, len);
            if (status > 0) handle->offset += (uint32_t)status;
            return status;
        }
    }

    if (handle->kind == FD_KIND_PIPE) {
        pipe_t* pipe = handle->u.pipe_end.pipe;
        int total = 0;

        if (!pipe || handle->u.pipe_end.is_writer == 0U) return -1;
        if (pipe_reader_count(pipe) == 0U) return -1;

        for (;;) {
            int chunk = pipe_write_some(pipe, (const uint8_t*)buffer + total, len - (uint32_t)total);

            if (chunk < 0) return -1;
            total += chunk;
            if ((uint32_t)total == len) return total;
            if (total != 0 && pipe_reader_count(pipe) == 0U) return total;
            if (pipe_reader_count(pipe) == 0U) return -1;
            if (fd_should_abort_wait(proc)) return -1;
            process_yield();
        }
    }

    return -1;
}

int fd_close(process_t* proc, int fd) {
    fd_handle_t* handle;

    if (!proc || !fd_slot_valid(fd)) return -1;
    handle = proc->fd_table[fd];
    if (!handle) return -1;
    proc->fd_table[fd] = 0;
    fd_handle_release(handle);
    return 0;
}

int fd_dup2(process_t* proc, int oldfd, int newfd) {
    fd_handle_t* handle;

    if (!proc || !fd_slot_valid(oldfd) || !fd_slot_valid(newfd)) return -1;
    handle = proc->fd_table[oldfd];
    if (!handle) return -1;
    if (oldfd == newfd) return newfd;

    if (proc->fd_table[newfd]) (void)fd_close(proc, newfd);
    fd_handle_acquire(handle);
    proc->fd_table[newfd] = handle;
    return newfd;
}

int fd_pipe(process_t* proc, int out_fds[2]) {
    pipe_t* pipe;
    fd_handle_t* read_handle;
    fd_handle_t* write_handle;
    int read_fd;
    int write_fd;

    if (!proc || !out_fds) return -1;
    pipe = pipe_create();
    if (!pipe) return -1;

    read_handle = fd_create_pipe_handle(pipe, 0U);
    write_handle = fd_create_pipe_handle(pipe, 1U);
    if (!read_handle || !write_handle) {
        fd_handle_release(read_handle);
        fd_handle_release(write_handle);
        return -1;
    }

    read_fd = fd_find_free_slot(proc, 0);
    write_fd = read_fd >= 0 ? fd_find_free_slot(proc, read_fd + 1) : -1;
    if (read_fd < 0 || write_fd < 0) {
        fd_handle_release(read_handle);
        fd_handle_release(write_handle);
        return -1;
    }

    proc->fd_table[read_fd] = read_handle;
    proc->fd_table[write_fd] = write_handle;
    out_fds[0] = read_fd;
    out_fds[1] = write_fd;
    return 0;
}

int fd_open_file(process_t* proc, const char* path, uint32_t access, uint32_t open_flags, int target_fd) {
    int node_idx;
    fd_handle_t* handle;
    int installed_fd;

    if (!proc || !path || path[0] == '\0') return -1;
    node_idx = fs_find_node(path);
    if (node_idx < 0 && (open_flags & FD_OPEN_CREATE) != 0U) {
        if (fs_create_file(path) != 0) return -1;
        node_idx = fs_find_node(path);
    }
    if (node_idx < 0) return -1;
    if ((open_flags & FD_OPEN_TRUNC) != 0U && (access & FD_ACCESS_WRITE) != 0U) {
        if (fs_write_file_raw_by_idx(node_idx, 0, 0U) < 0) return -1;
    }

    handle = fd_create_file_handle(node_idx, access, open_flags);
    if (!handle) return -1;
    installed_fd = fd_install_handle(proc, handle, target_fd);
    if (installed_fd < 0) {
        fd_handle_release(handle);
        return -1;
    }
    return installed_fd;
}
