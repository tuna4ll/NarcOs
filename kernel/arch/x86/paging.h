#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include <stddef.h>

#define KERNEL_BOOT_STACK_TOP      0x02C00000U
#define KERNEL_BOOT_STACK_PAGES    8U
#define USER_DATA_WINDOW_BASE      0x40000000U
#define USER_DATA_WINDOW_SIZE      0x00400000U
#define PAGING_FLAG_WRITE          0x002U
#define PAGING_FLAG_USER           0x004U
#define PAGING_FLAG_WRITE_THROUGH  0x008U
#define PAGING_FLAG_CACHE_DISABLE  0x010U

void init_paging();
void* alloc_physical_page();
void free_physical_page(void* page);
void* alloc_physical_pages(size_t count);
void free_physical_pages(void* base, size_t count);
int paging_map_user_region(uint32_t virt_addr, uint32_t phys_addr, size_t size, uint32_t flags);
void paging_unmap_user_region(uint32_t virt_addr, size_t size);
void* paging_alloc_kernel_stack(size_t stack_pages, uint32_t* out_stack_top);
void* paging_map_physical(uint32_t phys_addr, size_t size, uint32_t flags);
void paging_unmap_virtual(void* virt_addr, size_t size);
uint32_t paging_kernel_stack_base();
uint32_t paging_kernel_stack_size();
uint32_t paging_kernel_vm_base();
uint32_t paging_kernel_vm_size();
uint32_t paging_total_frames();
uint32_t paging_used_frames();

#endif
