#ifndef EXEC_H
#define EXEC_H

#include <stdint.h>
#include "paging.h"

#define EXEC_OK               0
#define EXEC_ERR_INVALID     -1
#define EXEC_ERR_FORMAT      -2
#define EXEC_ERR_UNSUPPORTED -3
#define EXEC_ERR_BOUNDS      -4
#define EXEC_ERR_OVERLAP     -5
#define EXEC_ERR_MEMORY      -6
#define EXEC_ERR_IO          -7

#define EXEC_IMAGE_CLASS_NONE  0U
#define EXEC_IMAGE_CLASS_ELF32 1U
#define EXEC_IMAGE_CLASS_ELF64 2U

#if UINTPTR_MAX > 0xFFFFFFFFU
#define EXEC_KERNEL_SUPPORTS_ELF32 0U
#define EXEC_KERNEL_SUPPORTS_ELF64 1U
#define EXEC_KERNEL_NATIVE_IMAGE_CLASS EXEC_IMAGE_CLASS_ELF64
#else
#define EXEC_KERNEL_SUPPORTS_ELF32 1U
#define EXEC_KERNEL_SUPPORTS_ELF64 0U
#define EXEC_KERNEL_NATIVE_IMAGE_CLASS EXEC_IMAGE_CLASS_ELF32
#endif

#define EXEC_USER_IMAGE_BASE  (USER_DATA_WINDOW_BASE + 0x00200000U)
#define EXEC_USER_STACK_PAGES 16U
#define EXEC_USER_STACK_SIZE  (EXEC_USER_STACK_PAGES * 4096U)
#define EXEC_USER_STACK_BASE  (USER_DATA_WINDOW_BASE + USER_DATA_WINDOW_SIZE - EXEC_USER_STACK_SIZE)
#define EXEC_USER_STACK_TOP   (USER_DATA_WINDOW_BASE + USER_DATA_WINDOW_SIZE)
#define EXEC_USER_IMAGE_LIMIT EXEC_USER_STACK_BASE
#define EXEC_MAX_IMAGE_MAPPINGS 9U

typedef struct {
    uint32_t entry_point;
    uint32_t image_base;
    uint32_t image_limit;
    uint32_t program_break;
    uint32_t stack_base;
    uint32_t stack_top;
    uint32_t segment_count;
    uint8_t image_class;
    uint8_t reserved[3];
} exec_image_t;

typedef struct {
    uint32_t virt_base;
    uint32_t phys_base;
    uint32_t page_count;
    uint32_t flags;
} exec_mapping_t;

typedef struct {
    exec_image_t image;
    exec_mapping_t mappings[EXEC_MAX_IMAGE_MAPPINGS];
    uint32_t mapping_count;
    int valid;
} exec_address_space_t;

int exec_load_file(const char* path, exec_address_space_t* out_space);
int exec_load_elf32_file(const char* path, exec_address_space_t* out_space);
int exec_load_elf64_file(const char* path, exec_address_space_t* out_space);
void exec_release_address_space(exec_address_space_t* space);
int exec_activate_address_space(const exec_address_space_t* space);
int exec_query_image(const exec_address_space_t* space, exec_image_t* out_image);
const char* exec_error_string(int status);
const char* exec_supported_mode_string(void);

#endif
