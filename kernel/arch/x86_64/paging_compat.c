#include "paging.h"

#include "x64_paging.h"

void init_paging(void) {
    (void)x64_paging_init();
}

void* alloc_physical_page(void) {
    return x64_alloc_physical_page();
}

void free_physical_page(void* page) {
    free_x64_physical_page(page);
}

void* alloc_physical_pages(size_t count) {
    return x64_alloc_physical_pages(count);
}

void free_physical_pages(void* base, size_t count) {
    free_x64_physical_pages(base, count);
}

int paging_map_user_region(uintptr_t virt_addr, uintptr_t phys_addr, size_t size, uint64_t flags) {
    return x64_paging_map_user_region((uint64_t)virt_addr, (uint64_t)phys_addr, size, flags);
}

void paging_unmap_user_region(uintptr_t virt_addr, size_t size) {
    x64_paging_unmap_user_region((uint64_t)virt_addr, size);
}

void* paging_alloc_kernel_stack(size_t stack_pages, uintptr_t* out_stack_top) {
    uint8_t* base = (uint8_t*)x64_alloc_physical_pages(stack_pages);

    if (!base) return 0;
    if (out_stack_top) *out_stack_top = (uintptr_t)(base + stack_pages * 4096U);
    return base;
}

void* paging_map_physical(uintptr_t phys_addr, size_t size, uint64_t flags) {
    return x64_paging_map_physical((uint64_t)phys_addr, size, flags);
}

void paging_unmap_virtual(void* virt_addr, size_t size) {
    x64_paging_unmap_virtual(virt_addr, size);
}

uint64_t paging_kernel_stack_base(void) {
    return 0;
}

uint64_t paging_kernel_stack_size(void) {
    return 0;
}

uint64_t paging_kernel_vm_base(void) {
    return x64_paging_kernel_vm_base();
}

uint64_t paging_kernel_vm_size(void) {
    return x64_paging_kernel_vm_size();
}

uint64_t paging_total_frames(void) {
    return x64_paging_total_frames();
}

uint64_t paging_used_frames(void) {
    return x64_paging_used_frames();
}
