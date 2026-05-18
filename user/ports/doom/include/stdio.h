#ifndef NARCOS_DOOM_STDIO_H
#define NARCOS_DOOM_STDIO_H
#include <stdarg.h>
#include <stddef.h>
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
typedef struct doom_file FILE;
extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;
int printf(const char* fmt, ...);
int fprintf(FILE* f, const char* fmt, ...);
int vfprintf(FILE* f, const char* fmt, va_list ap);
int snprintf(char* buf, size_t len, const char* fmt, ...);
int vsnprintf(char* buf, size_t len, const char* fmt, va_list ap);
int sprintf(char* buf, const char* fmt, ...);
int putchar(int c);
int puts(const char* s);
FILE* fopen(const char* path, const char* mode);
size_t fread(void* ptr, size_t size, size_t nmemb, FILE* f);
size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* f);
int fseek(FILE* f, long offset, int whence);
long ftell(FILE* f);
int fflush(FILE* f);
int fclose(FILE* f);
int remove(const char* path);
int rename(const char* oldpath, const char* newpath);
int sscanf(const char* str, const char* fmt, ...);
#endif
