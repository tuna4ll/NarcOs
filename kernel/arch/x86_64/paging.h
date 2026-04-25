#ifndef PAGING_H
#define PAGING_H

#include <stddef.h>
#include <stdint.h>

#define KERNEL_BOOT_STACK_TOP      0x0ULL
#define KERNEL_BOOT_STACK_PAGES    8U
#define USER_DATA_WINDOW_BASE      0x0000000040000000ULL
#define USER_DATA_WINDOW_SIZE      0x0000000000400000ULL
#define PAGING_FLAG_WRITE          0x002ULL
#define PAGING_FLAG_USER           0x004ULL
#define PAGING_FLAG_WRITE_THROUGH  0x008ULL
#define PAGING_FLAG_CACHE_DISABLE  0x010ULL

void init_paging(void);
void* alloc_physical_page(void);
void free_physical_page(void* page);
void* alloc_physical_pages(size_t count);
void free_physical_pages(void* base, size_t count);
int paging_map_user_region(uintptr_t virt_addr, uintptr_t phys_addr, size_t size, uint64_t flags);
void paging_unmap_user_region(uintptr_t virt_addr, size_t size);
void* paging_alloc_kernel_stack(size_t stack_pages, uintptr_t* out_stack_top);
void* paging_map_physical(uintptr_t phys_addr, size_t size, uint64_t flags);
void paging_unmap_virtual(void* virt_addr, size_t size);
uint64_t paging_kernel_stack_base(void);
uint64_t paging_kernel_stack_size(void);
uint64_t paging_kernel_vm_base(void);
uint64_t paging_kernel_vm_size(void);
uint64_t paging_total_frames(void);
uint64_t paging_used_frames(void);

#endif
