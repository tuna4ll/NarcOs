#ifndef NARCOS_X86_64_PAGING_H
#define NARCOS_X86_64_PAGING_H

#include <stddef.h>
#include <stdint.h>

#define X64_PAGING_FLAG_WRITE          0x002ULL
#define X64_PAGING_FLAG_USER           0x004ULL
#define X64_PAGING_FLAG_WRITE_THROUGH  0x008ULL
#define X64_PAGING_FLAG_CACHE_DISABLE  0x010ULL

int x64_paging_init(void);

void* x64_alloc_physical_page(void);
void free_x64_physical_page(void* page);
void* x64_alloc_physical_pages(size_t count);
void free_x64_physical_pages(void* base, size_t count);

void* x64_paging_map_physical(uint64_t phys_addr, size_t size, uint64_t flags);
void x64_paging_unmap_virtual(void* virt_addr, size_t size);
int x64_paging_map_user_region(uint64_t virt_addr, uint64_t phys_addr, size_t size, uint64_t flags);
void x64_paging_unmap_user_region(uint64_t virt_addr, size_t size);

int x64_heap_init(void);
void* x64_heap_alloc(size_t size);
void x64_heap_free(void* ptr);

uint64_t x64_paging_heap_base(void);
uint64_t x64_paging_heap_size(void);
uint64_t x64_paging_kernel_vm_base(void);
uint64_t x64_paging_kernel_vm_size(void);
uint64_t x64_paging_total_frames(void);
uint64_t x64_paging_used_frames(void);

#endif
