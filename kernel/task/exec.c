#include "exec.h"
#include "fs.h"
#include "serial.h"
#include "string.h"

#define EXEC_PAGE_SIZE 4096U
#define EXEC_MAX_LOAD_SEGMENTS 8U
#define EXEC_MAX_PROGRAM_HEADERS 16U

#define EXEC_ELF_MAGIC0 0x7FU
#define EXEC_ELF_MAGIC1 'E'
#define EXEC_ELF_MAGIC2 'L'
#define EXEC_ELF_MAGIC3 'F'
#define EXEC_ELF_CLASS32 1U
#define EXEC_ELF_CLASS64 2U
#define EXEC_ELF_DATA_LE 1U
#define EXEC_ELF_VERSION_CURRENT 1U
#define EXEC_ELF_TYPE_EXEC 2U
#define EXEC_ELF_MACHINE_386 3U
#define EXEC_ELF_MACHINE_X86_64 62U
#define EXEC_ELF_PT_LOAD 1U
#define EXEC_ELF_PF_W 0x2U

typedef struct __attribute__((packed)) {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} exec_elf32_ehdr_t;

typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t offset;
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t flags;
    uint32_t align;
} exec_elf32_phdr_t;

typedef struct __attribute__((packed)) {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} exec_elf64_ehdr_t;

typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} exec_elf64_phdr_t;

typedef struct {
    uint32_t vaddr;
    uint32_t offset;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t flags;
    uint32_t map_base;
    uint32_t map_size;
    uint32_t page_count;
} exec_load_segment_t;

static const exec_address_space_t* exec_active_space = 0;
static void exec_unmap_address_space(const exec_address_space_t* space);

static void exec_log_failure(const char* path, const char* stage, int status) {
    serial_write("[exec] load failed path=");
    serial_write(path ? path : "<null>");
    if (stage && stage[0] != '\0') {
        serial_write(" stage=");
        serial_write(stage);
    }
    serial_write(" reason=");
    serial_write(exec_error_string(status));
    serial_write(" code=");
    serial_write_hex32((uint32_t)status);
    serial_write_char('\n');
}

static int exec_fail(const char* path, const char* stage, int status) {
    exec_log_failure(path, stage, status);
    return status;
}

static int exec_fail_segment(const char* path, const char* stage, int status, uint32_t segment_index) {
    exec_log_failure(path, stage, status);
    serial_write("[exec] segment=");
    serial_write_hex32(segment_index);
    serial_write_char('\n');
    return status;
}

static uint32_t exec_align_down(uint32_t value) {
    return value & ~(EXEC_PAGE_SIZE - 1U);
}

static uint32_t exec_align_up(uint32_t value) {
    return (value + EXEC_PAGE_SIZE - 1U) & ~(EXEC_PAGE_SIZE - 1U);
}

static int exec_user_range_ok(uint32_t start, uint32_t size) {
    uint64_t end;

    if (size == 0U) return 1;
    if (start < EXEC_USER_IMAGE_BASE) return 0;
    end = (uint64_t)start + (uint64_t)size;
    if (end <= (uint64_t)start) return 0;
    if (end > EXEC_USER_IMAGE_LIMIT) return 0;
    return 1;
}

static int exec_entry_in_segments(uint32_t entry, const exec_load_segment_t* segments, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        uint64_t end = (uint64_t)segments[i].vaddr + (uint64_t)segments[i].memsz;
        if (entry >= segments[i].vaddr && (uint64_t)entry < end) return 1;
    }
    return 0;
}

static int exec_aligned_range_overlaps(const exec_load_segment_t* segments, uint32_t count,
                                       uint32_t map_base, uint32_t map_size) {
    uint32_t map_end = map_base + map_size;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t other_base = segments[i].map_base;
        uint32_t other_end = other_base + segments[i].map_size;
        if (map_base < other_end && map_end > other_base) return 1;
    }
    return 0;
}

static int exec_record_mapping(exec_address_space_t* space, uint32_t virt_base,
                               uint32_t phys_base, uint32_t page_count, uint32_t flags) {
    if (!space) return EXEC_ERR_INVALID;
    if (space->mapping_count >= EXEC_MAX_IMAGE_MAPPINGS) return EXEC_ERR_MEMORY;
    space->mappings[space->mapping_count].virt_base = virt_base;
    space->mappings[space->mapping_count].phys_base = phys_base;
    space->mappings[space->mapping_count].page_count = page_count;
    space->mappings[space->mapping_count].flags = flags;
    space->mapping_count++;
    return EXEC_OK;
}

static int exec_commit_loaded_image(const char* path, exec_address_space_t* out_space,
                                    const exec_load_segment_t* segments, uint32_t load_segment_count,
                                    uint32_t entry_point, uint32_t image_low, uint32_t image_high,
                                    uint32_t final_brk, uint8_t image_class) {
    exec_address_space_t temp_space;
    uint32_t final_flags;

    if (!out_space) return exec_fail(path, "out-space", EXEC_ERR_INVALID);
    memset(&temp_space, 0, sizeof(temp_space));

    if (load_segment_count == 0U) return exec_fail(path, "no-load-segments", EXEC_ERR_UNSUPPORTED);
    if (!exec_entry_in_segments(entry_point, segments, load_segment_count)) {
        return exec_fail(path, "entry-outside-segments", EXEC_ERR_FORMAT);
    }
    if (final_brk > EXEC_USER_STACK_BASE) return exec_fail(path, "program-break", EXEC_ERR_BOUNDS);

    if (exec_active_space) {
        exec_unmap_address_space(exec_active_space);
        exec_active_space = 0;
    }

    for (uint32_t i = 0; i < load_segment_count; i++) {
        void* phys_base = alloc_physical_pages(segments[i].page_count);
        uint32_t segment_flags = PAGING_FLAG_WRITE;
        int status;

        if (!phys_base) {
            exec_release_address_space(&temp_space);
            return exec_fail_segment(path, "alloc-segment-pages", EXEC_ERR_MEMORY, i);
        }
        if (paging_map_user_region(segments[i].map_base, (uint32_t)(uintptr_t)phys_base,
                                   segments[i].map_size, segment_flags) != 0) {
            free_physical_pages(phys_base, segments[i].page_count);
            exec_release_address_space(&temp_space);
            return exec_fail_segment(path, "map-segment-temp", EXEC_ERR_MEMORY, i);
        }
        final_flags = (segments[i].flags & EXEC_ELF_PF_W) != 0U ? PAGING_FLAG_WRITE : 0U;
        if (exec_record_mapping(&temp_space, segments[i].map_base,
                                (uint32_t)(uintptr_t)phys_base, segments[i].page_count, final_flags) != EXEC_OK) {
            free_physical_pages(phys_base, segments[i].page_count);
            exec_release_address_space(&temp_space);
            return exec_fail_segment(path, "record-segment", EXEC_ERR_MEMORY, i);
        }

        memset((void*)(uintptr_t)segments[i].map_base, 0, segments[i].map_size);
        if (segments[i].filesz != 0U) {
            status = fs_read_file_raw(path, (void*)(uintptr_t)segments[i].vaddr,
                                      segments[i].offset, segments[i].filesz);
            if (status != (int)segments[i].filesz) {
                exec_release_address_space(&temp_space);
                return exec_fail_segment(path, "read-segment-data", EXEC_ERR_IO, i);
            }
        }

        if (paging_map_user_region(segments[i].map_base, (uint32_t)(uintptr_t)phys_base,
                                   segments[i].map_size, final_flags) != 0) {
            exec_release_address_space(&temp_space);
            return exec_fail_segment(path, "map-segment-final", EXEC_ERR_MEMORY, i);
        }
    }

    {
        void* stack_phys = alloc_physical_pages(EXEC_USER_STACK_PAGES);
        if (!stack_phys) {
            exec_release_address_space(&temp_space);
            return exec_fail(path, "alloc-stack", EXEC_ERR_MEMORY);
        }
        if (paging_map_user_region(EXEC_USER_STACK_BASE, (uint32_t)(uintptr_t)stack_phys,
                                   EXEC_USER_STACK_SIZE, PAGING_FLAG_WRITE) != 0) {
            free_physical_pages(stack_phys, EXEC_USER_STACK_PAGES);
            exec_release_address_space(&temp_space);
            return exec_fail(path, "map-stack", EXEC_ERR_MEMORY);
        }
        if (exec_record_mapping(&temp_space, EXEC_USER_STACK_BASE,
                                (uint32_t)(uintptr_t)stack_phys, EXEC_USER_STACK_PAGES,
                                PAGING_FLAG_WRITE) != EXEC_OK) {
            free_physical_pages(stack_phys, EXEC_USER_STACK_PAGES);
            exec_release_address_space(&temp_space);
            return exec_fail(path, "record-stack", EXEC_ERR_MEMORY);
        }
        memset((void*)(uintptr_t)EXEC_USER_STACK_BASE, 0, EXEC_USER_STACK_SIZE);
    }

    temp_space.image.entry_point = entry_point;
    temp_space.image.image_base = image_low;
    temp_space.image.image_limit = image_high;
    temp_space.image.program_break = final_brk;
    temp_space.image.stack_base = EXEC_USER_STACK_BASE;
    temp_space.image.stack_top = EXEC_USER_STACK_TOP;
    temp_space.image.segment_count = load_segment_count;
    temp_space.image.image_class = image_class;
    temp_space.valid = 1;

    exec_release_address_space(out_space);
    *out_space = temp_space;
    exec_active_space = out_space;
    return EXEC_OK;
}

static void exec_unmap_address_space(const exec_address_space_t* space) {
    if (!space || space->mapping_count == 0U) return;
    for (uint32_t i = 0; i < space->mapping_count; i++) {
        const exec_mapping_t* mapping = &space->mappings[i];
        paging_unmap_user_region(mapping->virt_base, mapping->page_count * EXEC_PAGE_SIZE);
    }
}

void exec_release_address_space(exec_address_space_t* space) {
    if (!space) return;
    if (exec_active_space == space) {
        exec_unmap_address_space(space);
        exec_active_space = 0;
    }
    while (space->mapping_count != 0U) {
        exec_mapping_t* mapping = &space->mappings[space->mapping_count - 1U];
        free_physical_pages((void*)(uintptr_t)mapping->phys_base, mapping->page_count);
        space->mapping_count--;
    }
    memset(&space->image, 0, sizeof(space->image));
    space->valid = 0;
}

int exec_activate_address_space(const exec_address_space_t* space) {
    if (!space || !space->valid) return EXEC_ERR_INVALID;
    if (exec_active_space == space) return EXEC_OK;
    if (exec_active_space) exec_unmap_address_space(exec_active_space);
    for (uint32_t i = 0; i < space->mapping_count; i++) {
        const exec_mapping_t* mapping = &space->mappings[i];
        if (paging_map_user_region(mapping->virt_base, mapping->phys_base,
                                   mapping->page_count * EXEC_PAGE_SIZE,
                                   mapping->flags) != 0) {
            exec_unmap_address_space(space);
            return EXEC_ERR_MEMORY;
        }
    }
    exec_active_space = space;
    return EXEC_OK;
}

int exec_query_image(const exec_address_space_t* space, exec_image_t* out_image) {
    if (!space || !out_image || !space->valid) return EXEC_ERR_INVALID;
    *out_image = space->image;
    return EXEC_OK;
}

const char* exec_error_string(int status) {
    switch (status) {
        case EXEC_OK: return "ok";
        case EXEC_ERR_INVALID: return "invalid argument";
        case EXEC_ERR_FORMAT: return "invalid elf format";
        case EXEC_ERR_UNSUPPORTED: return "unsupported executable";
        case EXEC_ERR_BOUNDS: return "out of exec window";
        case EXEC_ERR_OVERLAP: return "overlapping load segments";
        case EXEC_ERR_MEMORY: return "out of memory";
        case EXEC_ERR_IO: return "i/o error";
        default: return "unknown";
    }
}

const char* exec_supported_mode_string(void) {
#if EXEC_KERNEL_NATIVE_IMAGE_CLASS == EXEC_IMAGE_CLASS_ELF64
    return "ELF64 native only";
#else
    return "ELF32 native only";
#endif
}

int exec_load_file(const char* path, exec_address_space_t* out_space) {
    uint8_t ident[16];
    int file_idx;
    int status;

    if (!path || path[0] == '\0') return exec_fail(path, "path", EXEC_ERR_INVALID);
    if (!out_space) return exec_fail(path, "out-space", EXEC_ERR_INVALID);

    file_idx = fs_find_node(path);
    if (file_idx < 0) return exec_fail(path, "find-node", EXEC_ERR_IO);
    status = fs_read_file_raw(path, ident, 0U, sizeof(ident));
    if (status != (int)sizeof(ident)) return exec_fail(path, "read-ident", EXEC_ERR_IO);

    if (ident[0] != EXEC_ELF_MAGIC0 || ident[1] != EXEC_ELF_MAGIC1 ||
        ident[2] != EXEC_ELF_MAGIC2 || ident[3] != EXEC_ELF_MAGIC3) {
        return exec_fail(path, "bad-magic", EXEC_ERR_FORMAT);
    }

    if (ident[4] == EXEC_ELF_CLASS32) {
#if EXEC_KERNEL_SUPPORTS_ELF32
        return exec_load_elf32_file(path, out_space);
#else
        return exec_fail(path, "elf32-disabled", EXEC_ERR_UNSUPPORTED);
#endif
    }
    if (ident[4] == EXEC_ELF_CLASS64) {
#if EXEC_KERNEL_SUPPORTS_ELF64
        return exec_load_elf64_file(path, out_space);
#else
        return exec_fail(path, "elf64-disabled", EXEC_ERR_UNSUPPORTED);
#endif
    }
    return exec_fail(path, "unsupported-class", EXEC_ERR_UNSUPPORTED);
}

int exec_load_elf32_file(const char* path, exec_address_space_t* out_space) {
    exec_elf32_ehdr_t ehdr;
    exec_elf32_phdr_t phdrs[EXEC_MAX_PROGRAM_HEADERS];
    exec_load_segment_t segments[EXEC_MAX_LOAD_SEGMENTS];
    disk_fs_node_t node;
    uint32_t load_segment_count = 0U;
    uint32_t image_low = 0U;
    uint32_t image_high = 0U;
    uint32_t final_brk = 0U;
    int file_idx;
    int status;

    if (!path || path[0] == '\0') return exec_fail(path, "path", EXEC_ERR_INVALID);
    if (!out_space) return exec_fail(path, "out-space", EXEC_ERR_INVALID);

    file_idx = fs_find_node(path);
    if (file_idx < 0) return exec_fail(path, "find-node", EXEC_ERR_IO);
    if (fs_get_node_info(file_idx, &node) != 0 || node.flags != FS_NODE_FILE) {
        return exec_fail(path, "node-info", EXEC_ERR_IO);
    }
    if (node.size < sizeof(ehdr)) return exec_fail(path, "short-header", EXEC_ERR_FORMAT);

    status = fs_read_file_raw(path, &ehdr, 0U, sizeof(ehdr));
    if (status != (int)sizeof(ehdr)) return exec_fail(path, "read-ehdr", EXEC_ERR_IO);

    if (ehdr.ident[0] != EXEC_ELF_MAGIC0 || ehdr.ident[1] != EXEC_ELF_MAGIC1 ||
        ehdr.ident[2] != EXEC_ELF_MAGIC2 || ehdr.ident[3] != EXEC_ELF_MAGIC3) {
        return exec_fail(path, "bad-magic", EXEC_ERR_FORMAT);
    }
    if (ehdr.ident[4] != EXEC_ELF_CLASS32 || ehdr.ident[5] != EXEC_ELF_DATA_LE ||
        ehdr.ident[6] != EXEC_ELF_VERSION_CURRENT) {
        return exec_fail(path, "bad-ident", EXEC_ERR_UNSUPPORTED);
    }
    if (ehdr.type != EXEC_ELF_TYPE_EXEC || ehdr.machine != EXEC_ELF_MACHINE_386 ||
        ehdr.version != EXEC_ELF_VERSION_CURRENT) {
        return exec_fail(path, "bad-target", EXEC_ERR_UNSUPPORTED);
    }
    if (ehdr.ehsize != sizeof(ehdr) || ehdr.phentsize != sizeof(exec_elf32_phdr_t)) {
        return exec_fail(path, "bad-header-size", EXEC_ERR_FORMAT);
    }
    if (ehdr.phnum == 0U || ehdr.phnum > EXEC_MAX_PROGRAM_HEADERS) {
        return exec_fail(path, "bad-phnum", EXEC_ERR_UNSUPPORTED);
    }
    if ((uint64_t)ehdr.phoff + (uint64_t)ehdr.phnum * sizeof(exec_elf32_phdr_t) > node.size) {
        return exec_fail(path, "phdr-bounds", EXEC_ERR_FORMAT);
    }

    status = fs_read_file_raw(path, phdrs, ehdr.phoff, ehdr.phnum * sizeof(exec_elf32_phdr_t));
    if (status != (int)(ehdr.phnum * sizeof(exec_elf32_phdr_t))) {
        return exec_fail(path, "read-phdrs", EXEC_ERR_IO);
    }

    for (uint32_t i = 0; i < ehdr.phnum; i++) {
        exec_load_segment_t* segment;
        uint32_t map_base;
        uint32_t map_end;
        uint64_t seg_end;

        if (phdrs[i].type != EXEC_ELF_PT_LOAD || phdrs[i].memsz == 0U) continue;
        if (load_segment_count >= EXEC_MAX_LOAD_SEGMENTS) {
            return exec_fail_segment(path, "too-many-load-segments", EXEC_ERR_UNSUPPORTED, i);
        }
        if (phdrs[i].filesz > phdrs[i].memsz) return exec_fail_segment(path, "filesz>memsz", EXEC_ERR_FORMAT, i);
        if ((uint64_t)phdrs[i].offset + (uint64_t)phdrs[i].filesz > node.size) {
            return exec_fail_segment(path, "segment-file-bounds", EXEC_ERR_FORMAT, i);
        }
        if (!exec_user_range_ok(phdrs[i].vaddr, phdrs[i].memsz)) {
            return exec_fail_segment(path, "segment-user-range", EXEC_ERR_BOUNDS, i);
        }

        map_base = exec_align_down(phdrs[i].vaddr);
        seg_end = (uint64_t)phdrs[i].vaddr + (uint64_t)phdrs[i].memsz;
        map_end = exec_align_up((uint32_t)seg_end);
        if (map_end <= map_base) return exec_fail_segment(path, "segment-map-wrap", EXEC_ERR_BOUNDS, i);
        if (map_end > EXEC_USER_IMAGE_LIMIT) return exec_fail_segment(path, "segment-map-limit", EXEC_ERR_BOUNDS, i);
        if (exec_aligned_range_overlaps(segments, load_segment_count, map_base, map_end - map_base)) {
            return exec_fail_segment(path, "segment-overlap", EXEC_ERR_OVERLAP, i);
        }

        segment = &segments[load_segment_count++];
        segment->vaddr = phdrs[i].vaddr;
        segment->offset = phdrs[i].offset;
        segment->filesz = phdrs[i].filesz;
        segment->memsz = phdrs[i].memsz;
        segment->flags = phdrs[i].flags;
        segment->map_base = map_base;
        segment->map_size = map_end - map_base;
        segment->page_count = segment->map_size / EXEC_PAGE_SIZE;

        if (image_low == 0U || segment->map_base < image_low) image_low = segment->map_base;
        if (map_end > image_high) image_high = map_end;
        if (exec_align_up((uint32_t)seg_end) > final_brk) {
            final_brk = exec_align_up((uint32_t)seg_end);
        }
    }

    return exec_commit_loaded_image(path, out_space, segments, load_segment_count,
                                    ehdr.entry, image_low, image_high, final_brk,
                                    EXEC_IMAGE_CLASS_ELF32);
}

int exec_load_elf64_file(const char* path, exec_address_space_t* out_space) {
    exec_elf64_ehdr_t ehdr;
    exec_elf64_phdr_t phdrs[EXEC_MAX_PROGRAM_HEADERS];
    exec_load_segment_t segments[EXEC_MAX_LOAD_SEGMENTS];
    disk_fs_node_t node;
    uint32_t load_segment_count = 0U;
    uint32_t image_low = 0U;
    uint32_t image_high = 0U;
    uint32_t final_brk = 0U;
    int file_idx;
    int status;

    if (!path || path[0] == '\0') return exec_fail(path, "path", EXEC_ERR_INVALID);
    if (!out_space) return exec_fail(path, "out-space", EXEC_ERR_INVALID);

    file_idx = fs_find_node(path);
    if (file_idx < 0) return exec_fail(path, "find-node", EXEC_ERR_IO);
    if (fs_get_node_info(file_idx, &node) != 0 || node.flags != FS_NODE_FILE) {
        return exec_fail(path, "node-info", EXEC_ERR_IO);
    }
    if (node.size < sizeof(ehdr)) return exec_fail(path, "short-header", EXEC_ERR_FORMAT);

    status = fs_read_file_raw(path, &ehdr, 0U, sizeof(ehdr));
    if (status != (int)sizeof(ehdr)) return exec_fail(path, "read-ehdr", EXEC_ERR_IO);

    if (ehdr.ident[0] != EXEC_ELF_MAGIC0 || ehdr.ident[1] != EXEC_ELF_MAGIC1 ||
        ehdr.ident[2] != EXEC_ELF_MAGIC2 || ehdr.ident[3] != EXEC_ELF_MAGIC3) {
        return exec_fail(path, "bad-magic", EXEC_ERR_FORMAT);
    }
    if (ehdr.ident[4] != EXEC_ELF_CLASS64 || ehdr.ident[5] != EXEC_ELF_DATA_LE ||
        ehdr.ident[6] != EXEC_ELF_VERSION_CURRENT) {
        return exec_fail(path, "bad-ident", EXEC_ERR_UNSUPPORTED);
    }
    if (ehdr.type != EXEC_ELF_TYPE_EXEC || ehdr.machine != EXEC_ELF_MACHINE_X86_64 ||
        ehdr.version != EXEC_ELF_VERSION_CURRENT) {
        return exec_fail(path, "bad-target", EXEC_ERR_UNSUPPORTED);
    }
    if (ehdr.ehsize != sizeof(ehdr) || ehdr.phentsize != sizeof(exec_elf64_phdr_t)) {
        return exec_fail(path, "bad-header-size", EXEC_ERR_FORMAT);
    }
    if (ehdr.phnum == 0U || ehdr.phnum > EXEC_MAX_PROGRAM_HEADERS) {
        return exec_fail(path, "bad-phnum", EXEC_ERR_UNSUPPORTED);
    }
    if ((uint64_t)ehdr.phoff + (uint64_t)ehdr.phnum * sizeof(exec_elf64_phdr_t) > node.size) {
        return exec_fail(path, "phdr-bounds", EXEC_ERR_FORMAT);
    }
    if (ehdr.entry > 0xFFFFFFFFULL) return exec_fail(path, "entry-width", EXEC_ERR_BOUNDS);

    status = fs_read_file_raw(path, phdrs, (uint32_t)ehdr.phoff, ehdr.phnum * sizeof(exec_elf64_phdr_t));
    if (status != (int)(ehdr.phnum * sizeof(exec_elf64_phdr_t))) {
        return exec_fail(path, "read-phdrs", EXEC_ERR_IO);
    }

    for (uint32_t i = 0; i < ehdr.phnum; i++) {
        exec_load_segment_t* segment;
        uint32_t map_base;
        uint32_t map_end;
        uint64_t seg_end;

        if (phdrs[i].type != EXEC_ELF_PT_LOAD || phdrs[i].memsz == 0U) continue;
        if (load_segment_count >= EXEC_MAX_LOAD_SEGMENTS) {
            return exec_fail_segment(path, "too-many-load-segments", EXEC_ERR_UNSUPPORTED, i);
        }
        if (phdrs[i].filesz > phdrs[i].memsz) return exec_fail_segment(path, "filesz>memsz", EXEC_ERR_FORMAT, i);
        if (phdrs[i].offset > 0xFFFFFFFFULL || phdrs[i].filesz > 0xFFFFFFFFULL ||
            phdrs[i].memsz > 0xFFFFFFFFULL || phdrs[i].vaddr > 0xFFFFFFFFULL) {
            return exec_fail_segment(path, "segment-width", EXEC_ERR_BOUNDS, i);
        }
        if ((uint64_t)phdrs[i].offset + (uint64_t)phdrs[i].filesz > node.size) {
            return exec_fail_segment(path, "segment-file-bounds", EXEC_ERR_FORMAT, i);
        }
        if (!exec_user_range_ok((uint32_t)phdrs[i].vaddr, (uint32_t)phdrs[i].memsz)) {
            return exec_fail_segment(path, "segment-user-range", EXEC_ERR_BOUNDS, i);
        }

        map_base = exec_align_down((uint32_t)phdrs[i].vaddr);
        seg_end = phdrs[i].vaddr + phdrs[i].memsz;
        if (seg_end > 0xFFFFFFFFULL) return exec_fail_segment(path, "segment-end-width", EXEC_ERR_BOUNDS, i);
        map_end = exec_align_up((uint32_t)seg_end);
        if (map_end <= map_base) return exec_fail_segment(path, "segment-map-wrap", EXEC_ERR_BOUNDS, i);
        if (map_end > EXEC_USER_IMAGE_LIMIT) return exec_fail_segment(path, "segment-map-limit", EXEC_ERR_BOUNDS, i);
        if (exec_aligned_range_overlaps(segments, load_segment_count, map_base, map_end - map_base)) {
            return exec_fail_segment(path, "segment-overlap", EXEC_ERR_OVERLAP, i);
        }

        segment = &segments[load_segment_count++];
        segment->vaddr = (uint32_t)phdrs[i].vaddr;
        segment->offset = (uint32_t)phdrs[i].offset;
        segment->filesz = (uint32_t)phdrs[i].filesz;
        segment->memsz = (uint32_t)phdrs[i].memsz;
        segment->flags = phdrs[i].flags;
        segment->map_base = map_base;
        segment->map_size = map_end - map_base;
        segment->page_count = segment->map_size / EXEC_PAGE_SIZE;

        if (image_low == 0U || segment->map_base < image_low) image_low = segment->map_base;
        if (map_end > image_high) image_high = map_end;
        if (exec_align_up((uint32_t)seg_end) > final_brk) {
            final_brk = exec_align_up((uint32_t)seg_end);
        }
    }

    return exec_commit_loaded_image(path, out_space, segments, load_segment_count,
                                    (uint32_t)ehdr.entry, image_low, image_high, final_brk,
                                    EXEC_IMAGE_CLASS_ELF64);
}
