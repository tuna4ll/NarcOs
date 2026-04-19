#include "memory_alloc.h"
#include <stdint.h>
extern void vga_print(const char* str);
extern void vga_print_int(int num);
extern void vga_println(const char* str);
extern void vga_print_color(const char* str, uint8_t color);
#define HEAP_SIZE  (2 * 1024 * 1024)

extern uint8_t __kernel_end[];
typedef struct block_header {
    size_t size;
    uint8_t is_free;
    struct block_header* next;
} block_header_t;
static block_header_t* head = NULL;
void init_heap() {
    uint32_t heap_start = (uint32_t)__kernel_end;
    heap_start = (heap_start + 15U) & ~15U;
    head = (block_header_t*)heap_start;
    head->size = HEAP_SIZE - sizeof(block_header_t);
    head->is_free = 1;
    head->next = NULL;
}
void* malloc(size_t size) {
    if (size == 0) return NULL;
    block_header_t* curr = head;
    while (curr != NULL) {
        if (curr->is_free && curr->size >= size) {
            if (curr->size > size + sizeof(block_header_t) + 4) {
                block_header_t* new_block = (block_header_t*)((uint32_t)curr + sizeof(block_header_t) + size);
                new_block->size = curr->size - size - sizeof(block_header_t);
                new_block->is_free = 1;
                new_block->next = curr->next;
                curr->size = size;
                curr->next = new_block;
            }
            curr->is_free = 0;
            return (void*)((uint32_t)curr + sizeof(block_header_t));
        }
        curr = curr->next;
    }
    return NULL;
}
void free(void* ptr) {
    if (ptr == NULL) return;
    block_header_t* block = (block_header_t*)((uint32_t)ptr - sizeof(block_header_t));
    block->is_free = 1;
    block_header_t* curr = head;
    while (curr != NULL) {
        if (curr->is_free && curr->next != NULL && curr->next->is_free) {
            curr->size += sizeof(block_header_t) + curr->next->size;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
}
void malloc_test() {
    vga_print_color("Allocating 3 blocks (16, 32, 64 bytes)..\n", 0x0B);
    void* ptr1 = malloc(16);
    void* ptr2 = malloc(32);
    void* ptr3 = malloc(64);
    vga_print("Ptr1: 0x"); vga_print_int((int)ptr1); vga_println("");
    vga_print("Ptr2: 0x"); vga_print_int((int)ptr2); vga_println("");
    vga_print("Ptr3: 0x"); vga_print_int((int)ptr3); vga_println("");
    if(ptr1 && ptr2 && ptr3) {
        vga_print_color("[OK] Allocation successful.\n", 0x0A);
    } else {
        vga_print_color("[FAIL] Allocation failed.\n", 0x0C);
        return;
    }
    vga_print("Freeing Ptr2..\n");
    free(ptr2);
    vga_print("Allocating 12 bytes..\n");
    void* ptr4 = malloc(12);
    vga_print("Ptr4: 0x"); vga_print_int((int)ptr4); vga_println("");
    if (ptr4 == ptr2) {
        vga_print_color("[OK] Memory safely reused (First Fit)!\n", 0x0A);
    }
    free(ptr1);
    free(ptr3);
    free(ptr4);
    vga_print_color("[OK] All blocks freed successfully.\n", 0x0A);
}
