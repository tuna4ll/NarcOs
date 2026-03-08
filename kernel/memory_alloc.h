#ifndef MEMORY_ALLOC_H
#define MEMORY_ALLOC_H

#include <stdint.h>
#include <stddef.h>

void init_heap();
void* malloc(size_t size);
void free(void* ptr);

void malloc_test(); // Function to test malloc/free functionality in the shell

#endif // MEMORY_ALLOC_H
