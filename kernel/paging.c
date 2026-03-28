#include "paging.h"
#include "string.h"

#define PAGE_SIZE 4096U
#define LARGE_PAGE_SIZE 0x400000U
#define MAX_MANAGED_RAM (64U * 1024U * 1024U)
#define MAX_FRAMES (MAX_MANAGED_RAM / PAGE_SIZE)
#define BITMAP_SIZE (MAX_FRAMES / 8)
#define KERNEL_RESERVED_END 0x03000000U
#define MIN_FRAME_ADDR 0x00400000U

#define E820_COUNT_PTR ((uint16_t*)0x5000)
#define E820_DATA_PTR  ((uint8_t*)0x5002)
#define PDE_PRESENT 0x001U
#define PDE_RW      0x002U
#define PDE_USER    0x004U
#define PDE_PS      0x080U

typedef struct {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t ext_attr;
} __attribute__((packed)) e820_entry_t;

extern void load_page_directory(uint32_t* page_directory);
extern void enable_paging();

static uint32_t kernel_page_directory[1024] __attribute__((aligned(4096)));
static uint8_t frame_bitmap[BITMAP_SIZE];
static uint32_t total_frames = 0;
static uint32_t used_frames = 0;

static void frame_mark(uint32_t frame, int used) {
    uint8_t mask = (uint8_t)(1U << (frame & 7U));
    uint8_t* slot = &frame_bitmap[frame >> 3U];
    int was_used = (*slot & mask) != 0;
    if (used && !was_used) {
        *slot |= mask;
        used_frames++;
    } else if (!used && was_used) {
        *slot &= (uint8_t)~mask;
        used_frames--;
    }
}

static void reserve_range(uint32_t start, uint32_t end) {
    if (end <= start) return;
    uint32_t first = start / PAGE_SIZE;
    uint32_t last = (end + PAGE_SIZE - 1U) / PAGE_SIZE;
    if (last > total_frames) last = total_frames;
    for (uint32_t frame = first; frame < last; frame++) {
        frame_mark(frame, 1);
    }
}

static void free_usable_ranges() {
    uint16_t entry_count = *E820_COUNT_PTR;
    e820_entry_t* entries = (e820_entry_t*)E820_DATA_PTR;

    memset(frame_bitmap, 0xFF, sizeof(frame_bitmap));
    total_frames = MAX_FRAMES;
    used_frames = total_frames;

    if (entry_count == 0) {
        for (uint32_t frame = MIN_FRAME_ADDR / PAGE_SIZE; frame < total_frames; frame++) {
            frame_mark(frame, 0);
        }
        return;
    }

    uint64_t max_addr = 0;
    for (uint16_t i = 0; i < entry_count; i++) {
        if (entries[i].type != 1) continue;
        uint64_t region_end = entries[i].base_addr + entries[i].length;
        if (region_end > max_addr) max_addr = region_end;
    }
    if (max_addr != 0 && max_addr < MAX_MANAGED_RAM) {
        total_frames = (uint32_t)(max_addr / PAGE_SIZE);
        if (total_frames == 0) total_frames = MAX_FRAMES;
        used_frames = total_frames;
    }

    for (uint16_t i = 0; i < entry_count; i++) {
        if (entries[i].type != 1) continue;
        uint64_t region_start = entries[i].base_addr;
        uint64_t region_end = entries[i].base_addr + entries[i].length;
        if (region_end <= MIN_FRAME_ADDR) continue;
        if (region_start < MIN_FRAME_ADDR) region_start = MIN_FRAME_ADDR;
        if (region_start >= (uint64_t)total_frames * PAGE_SIZE) continue;
        if (region_end > (uint64_t)total_frames * PAGE_SIZE) {
            region_end = (uint64_t)total_frames * PAGE_SIZE;
        }
        reserve_range((uint32_t)region_start, (uint32_t)region_end);
        for (uint32_t frame = (uint32_t)region_start / PAGE_SIZE; frame < (uint32_t)region_end / PAGE_SIZE; frame++) {
            frame_mark(frame, 0);
        }
    }
}

static void map_large_identity_region(uint32_t start, uint32_t end, uint32_t flags) {
    uint32_t aligned_start = start & ~(LARGE_PAGE_SIZE - 1U);
    uint32_t aligned_end = (end + LARGE_PAGE_SIZE - 1U) & ~(LARGE_PAGE_SIZE - 1U);
    for (uint32_t addr = aligned_start; addr < aligned_end; addr += LARGE_PAGE_SIZE) {
        uint32_t pde = addr / LARGE_PAGE_SIZE;
        kernel_page_directory[pde] = addr | flags | PDE_PS;
    }
}

void init_paging() {
    free_usable_ranges();
    reserve_range(0, KERNEL_RESERVED_END);

    memset(kernel_page_directory, 0, sizeof(kernel_page_directory));
    map_large_identity_region(0, 0x04000000U, PDE_PRESENT | PDE_RW | PDE_USER);

    uint32_t framebuffer = *(uint32_t*)(0x6100 + 40);
    if (framebuffer != 0) {
        map_large_identity_region(framebuffer, framebuffer + 0x00800000U, PDE_PRESENT | PDE_RW);
    }

    load_page_directory(kernel_page_directory);
    enable_paging();
}

void* alloc_physical_page() {
    return alloc_physical_pages(1);
}

void free_physical_page(void* page) {
    free_physical_pages(page, 1);
}

void* alloc_physical_pages(size_t count) {
    if (count == 0 || total_frames == 0) return 0;
    uint32_t needed = (uint32_t)count;
    uint32_t run = 0;
    uint32_t start = 0;
    uint32_t min_frame = KERNEL_RESERVED_END / PAGE_SIZE;
    if (min_frame >= total_frames) return 0;

    for (uint32_t frame = min_frame; frame < total_frames; frame++) {
        uint8_t mask = (uint8_t)(1U << (frame & 7U));
        if ((frame_bitmap[frame >> 3U] & mask) == 0) {
            if (run == 0) start = frame;
            run++;
            if (run == needed) {
                for (uint32_t i = 0; i < needed; i++) frame_mark(start + i, 1);
                return (void*)(start * PAGE_SIZE);
            }
        } else {
            run = 0;
        }
    }
    return 0;
}

void free_physical_pages(void* base, size_t count) {
    if (!base || count == 0) return;
    uint32_t start = (uint32_t)base / PAGE_SIZE;
    uint32_t end = start + (uint32_t)count;
    if (end > total_frames) end = total_frames;
    for (uint32_t frame = start; frame < end; frame++) {
        if (frame >= KERNEL_RESERVED_END / PAGE_SIZE) frame_mark(frame, 0);
    }
}

uint32_t paging_total_frames() {
    return total_frames;
}

uint32_t paging_used_frames() {
    return used_frames;
}
