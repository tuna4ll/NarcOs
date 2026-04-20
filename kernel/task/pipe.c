#include "pipe.h"
#include "memory_alloc.h"
#include "string.h"

static void pipe_destroy_if_unused(pipe_t* pipe) {
    if (!pipe) return;
    if (pipe->readers == 0U && pipe->writers == 0U) free(pipe);
}

pipe_t* pipe_create(void) {
    pipe_t* pipe = (pipe_t*)malloc(sizeof(pipe_t));

    if (!pipe) return 0;
    memset(pipe, 0, sizeof(*pipe));
    return pipe;
}

void pipe_acquire_reader(pipe_t* pipe) {
    if (!pipe) return;
    pipe->readers++;
}

void pipe_release_reader(pipe_t* pipe) {
    if (!pipe || pipe->readers == 0U) return;
    pipe->readers--;
    pipe_destroy_if_unused(pipe);
}

void pipe_acquire_writer(pipe_t* pipe) {
    if (!pipe) return;
    pipe->writers++;
}

void pipe_release_writer(pipe_t* pipe) {
    if (!pipe || pipe->writers == 0U) return;
    pipe->writers--;
    pipe_destroy_if_unused(pipe);
}

uint32_t pipe_reader_count(const pipe_t* pipe) {
    return pipe ? pipe->readers : 0U;
}

uint32_t pipe_writer_count(const pipe_t* pipe) {
    return pipe ? pipe->writers : 0U;
}

uint32_t pipe_readable_bytes(const pipe_t* pipe) {
    return pipe ? pipe->size : 0U;
}

uint32_t pipe_writable_bytes(const pipe_t* pipe) {
    return pipe ? (PIPE_BUFFER_SIZE - pipe->size) : 0U;
}

int pipe_read_some(pipe_t* pipe, void* buffer, uint32_t len) {
    uint8_t* out = (uint8_t*)buffer;
    uint32_t copied = 0U;

    if (!pipe) return -1;
    if (!out && len != 0U) return -1;

    while (copied < len && pipe->size != 0U) {
        out[copied++] = pipe->buffer[pipe->read_pos];
        pipe->read_pos = (pipe->read_pos + 1U) % PIPE_BUFFER_SIZE;
        pipe->size--;
    }
    return (int)copied;
}

int pipe_write_some(pipe_t* pipe, const void* buffer, uint32_t len) {
    const uint8_t* in = (const uint8_t*)buffer;
    uint32_t written = 0U;

    if (!pipe) return -1;
    if (!in && len != 0U) return -1;

    while (written < len && pipe->size < PIPE_BUFFER_SIZE) {
        pipe->buffer[pipe->write_pos] = in[written++];
        pipe->write_pos = (pipe->write_pos + 1U) % PIPE_BUFFER_SIZE;
        pipe->size++;
    }
    return (int)written;
}
