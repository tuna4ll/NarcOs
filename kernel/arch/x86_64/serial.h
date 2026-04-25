#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

void serial_init(void);
int serial_is_ready(void);
void serial_write_char(char c);
void serial_write(const char* text);
void serial_write_line(const char* text);
void serial_write_hex32(uint32_t value);
void serial_write_hex64(uint64_t value);
int serial_copy_ring_buffer(char* out, int max_len);

#endif
