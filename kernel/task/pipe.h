#ifndef PIPE_H
#define PIPE_H

#include <stdint.h>

#define PIPE_BUFFER_SIZE 1024U

typedef struct pipe {
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t size;
    uint32_t readers;
    uint32_t writers;
    uint8_t buffer[PIPE_BUFFER_SIZE];
} pipe_t;

pipe_t* pipe_create(void);
void pipe_acquire_reader(pipe_t* pipe);
void pipe_release_reader(pipe_t* pipe);
void pipe_acquire_writer(pipe_t* pipe);
void pipe_release_writer(pipe_t* pipe);
uint32_t pipe_reader_count(const pipe_t* pipe);
uint32_t pipe_writer_count(const pipe_t* pipe);
uint32_t pipe_readable_bytes(const pipe_t* pipe);
uint32_t pipe_writable_bytes(const pipe_t* pipe);
int pipe_read_some(pipe_t* pipe, void* buffer, uint32_t len);
int pipe_write_some(pipe_t* pipe, const void* buffer, uint32_t len);

#endif
