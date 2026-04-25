#include "memory_alloc.h"

#include "serial.h"
#include "x64_paging.h"

void init_heap(void) {
    (void)x64_heap_init();
}

void* malloc(size_t size) {
    return x64_heap_alloc(size);
}

void free(void* ptr) {
    x64_heap_free(ptr);
}

void malloc_test(void) {
    void* block_a = malloc(32U);
    void* block_b = malloc(96U);

    if (!block_a || !block_b) {
        serial_write_line("malloc_test: failed");
        return;
    }
    free(block_a);
    free(block_b);
    serial_write_line("malloc_test: ok");
}
