#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include <stddef.h>

void init_paging();
void* alloc_physical_page();
void free_physical_page(void* page);
void* alloc_physical_pages(size_t count);
void free_physical_pages(void* base, size_t count);
uint32_t paging_total_frames();
uint32_t paging_used_frames();

#endif
