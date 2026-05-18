#ifndef NARCOS_DOOM_STDLIB_H
#define NARCOS_DOOM_STDLIB_H
#include <stddef.h>
#ifndef NULL
#define NULL ((void*)0)
#endif
void* malloc(size_t size);
void free(void* ptr);
void* calloc(size_t nmemb, size_t size);
void* realloc(void* ptr, size_t size);
char* getenv(const char* name);
int system(const char* command);
void exit(int code);
void abort(void);
int atoi(const char* s);
#ifndef NARCOS_DOOM_NO_FLOAT
double atof(const char* s);
#endif
int abs(int v);
#endif
