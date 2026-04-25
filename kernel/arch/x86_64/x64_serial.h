#ifndef NARCOS_X86_64_SERIAL_H
#define NARCOS_X86_64_SERIAL_H

#include <stdint.h>

void x64_serial_init(void);
void x64_serial_write_char(char c);
void x64_serial_write(const char* text);
void x64_serial_write_line(const char* text);
void x64_serial_write_hex32(uint32_t value);
void x64_serial_write_hex64(uint64_t value);

#endif
