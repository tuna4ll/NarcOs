#include "paging.h"
#include "string.h"

#define PAGE_SIZE 4096U
#define LARGE_PAGE_SIZE 0x400000U
#define LEGACY_FALLBACK_RAM (64U * 1024U * 1024U)
#define MAX_MANAGED_PHYS_ADDR 0x100000000ULL
#define MAX_FRAMES ((uint32_t)(MAX_MANAGED_PHYS_ADDR / PAGE_SIZE))
#define BITMAP_SIZE (MAX_FRAMES / 8U)
#define LOW_IDENTITY_PAGE_COUNT (LARGE_PAGE_SIZE / PAGE_SIZE)
#define KERNEL_RESERVED_END 0x03000000U
#define MIN_FRAME_ADDR 0x00400000U
#define KERNEL_STACK_WINDOW_SIZE LARGE_PAGE_SIZE
#define KERNEL_STACK_WINDOW_BASE (KERNEL_RESERVED_END - (2U * LARGE_PAGE_SIZE))
#define KERNEL_STACK_WINDOW_TOP  (KERNEL_STACK_WINDOW_BASE + KERNEL_STACK_WINDOW_SIZE)
#define KERNEL_STACK_WINDOW_PDE  (KERNEL_STACK_WINDOW_BASE / LARGE_PAGE_SIZE)
#define KERNEL_STACK_PAGE_COUNT  (KERNEL_STACK_WINDOW_SIZE / PAGE_SIZE)
#define KERNEL_STACK_GUARD_PAGES 1U
#define KERNEL_VM_WINDOW_SIZE LARGE_PAGE_SIZE
#define KERNEL_VM_WINDOW_BASE (KERNEL_RESERVED_END - KERNEL_VM_WINDOW_SIZE)
#define KERNEL_VM_WINDOW_PDE  (KERNEL_VM_WINDOW_BASE / LARGE_PAGE_SIZE)
#define KERNEL_VM_PAGE_COUNT  (KERNEL_VM_WINDOW_SIZE / PAGE_SIZE)
#define USER_DATA_WINDOW_PDE  (USER_DATA_WINDOW_BASE / LARGE_PAGE_SIZE)
#define USER_DATA_PAGE_COUNT  (USER_DATA_WINDOW_SIZE / PAGE_SIZE)

#define E820_COUNT_PTR ((uint16_t*)0x5000)
#define E820_DATA_PTR  ((uint8_t*)0x5002)
#define PDE_PRESENT 0x001U
#define PDE_RW      0x002U
#define PDE_USER    0x004U
#define PDE_PS      0x080U
#define PTE_PRESENT 0x001U

typedef struct {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t ext_attr;
} __attribute__((packed)) e820_entry_t;

extern uint8_t __user_region_start[];
extern uint8_t __user_region_end[];
extern void load_page_directory(uint32_t* page_directory);
extern void enable_paging();

static uint32_t low_identity_page_table[1024] __attribute__((aligned(4096)));
static uint32_t kernel_page_directory[1024] __attribute__((aligned(4096)));
static uint32_t kernel_stack_page_table[1024] __attribute__((aligned(4096)));
static uint32_t kernel_vm_page_table[1024] __attribute__((aligned(4096)));
static uint32_t user_data_page_table[1024] __attribute__((aligned(4096)));
static uint8_t frame_bitmap[BITMAP_SIZE];
static uint8_t kernel_stack_slot_bitmap[KERNEL_STACK_PAGE_COUNT / 8U];
static uint8_t kernel_vm_slot_bitmap[KERNEL_VM_PAGE_COUNT / 8U];
static uint32_t total_frames = 0;
static uint32_t used_frames = 0;
static uint64_t managed_phys_limit = 0;

static uint64_t clamp_managed_phys_limit(uint64_t addr) {
    if (addr > MAX_MANAGED_PHYS_ADDR) return MAX_MANAGED_PHYS_ADDR;
    return addr;
}

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

static void paging_invalidate_page(void* addr) {
    asm volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

static uint32_t paging_allowed_pte_flags(uint32_t flags) {
    return flags & (PAGING_FLAG_WRITE | PAGING_FLAG_USER |
                    PAGING_FLAG_WRITE_THROUGH | PAGING_FLAG_CACHE_DISABLE);
}

static void low_identity_mark_user_range(uint32_t start_addr, uint32_t end_addr, uint32_t flags) {
    uint32_t first_page;
    uint32_t last_page;
    uint32_t allowed_flags = paging_allowed_pte_flags(flags) | PDE_USER;

    if (end_addr <= start_addr) return;
    if (start_addr >= LARGE_PAGE_SIZE) return;
    if (end_addr > LARGE_PAGE_SIZE) end_addr = LARGE_PAGE_SIZE;

    first_page = start_addr / PAGE_SIZE;
    last_page = (end_addr + PAGE_SIZE - 1U) / PAGE_SIZE;
    for (uint32_t page = first_page; page < last_page; page++) {
        low_identity_page_table[page] = (page * PAGE_SIZE) | PTE_PRESENT | allowed_flags;
    }
}

static int kernel_stack_slot_used(uint32_t slot) {
    uint8_t mask = (uint8_t)(1U << (slot & 7U));
    return (kernel_stack_slot_bitmap[slot >> 3U] & mask) != 0;
}

static void kernel_stack_set_slot(uint32_t slot, int used) {
    uint8_t mask = (uint8_t)(1U << (slot & 7U));
    if (used) kernel_stack_slot_bitmap[slot >> 3U] |= mask;
    else kernel_stack_slot_bitmap[slot >> 3U] &= (uint8_t)~mask;
}

static int kernel_stack_reserve_slots(uint32_t count) {
    uint32_t run = 0;
    uint32_t start = 0;

    if (count == 0 || count > KERNEL_STACK_PAGE_COUNT) return -1;
    for (uint32_t slot = 0; slot < KERNEL_STACK_PAGE_COUNT; slot++) {
        if (!kernel_stack_slot_used(slot)) {
            if (run == 0) start = slot;
            run++;
            if (run == count) {
                for (uint32_t i = 0; i < count; i++) kernel_stack_set_slot(start + i, 1);
                return (int)start;
            }
        } else {
            run = 0;
        }
    }
    return -1;
}

static int kernel_vm_slot_used(uint32_t slot) {
    uint8_t mask = (uint8_t)(1U << (slot & 7U));
    return (kernel_vm_slot_bitmap[slot >> 3U] & mask) != 0;
}

static void kernel_vm_set_slot(uint32_t slot, int used) {
    uint8_t mask = (uint8_t)(1U << (slot & 7U));
    if (used) kernel_vm_slot_bitmap[slot >> 3U] |= mask;
    else kernel_vm_slot_bitmap[slot >> 3U] &= (uint8_t)~mask;
}

static int kernel_vm_reserve_slots(uint32_t count) {
    uint32_t run = 0;
    uint32_t start = 0;

    if (count == 0 || count > KERNEL_VM_PAGE_COUNT) return -1;
    for (uint32_t slot = 0; slot < KERNEL_VM_PAGE_COUNT; slot++) {
        if (!kernel_vm_slot_used(slot)) {
            if (run == 0) start = slot;
            run++;
            if (run == count) {
                for (uint32_t i = 0; i < count; i++) kernel_vm_set_slot(start + i, 1);
                return (int)start;
            }
        } else {
            run = 0;
        }
    }
    return -1;
}

static void kernel_vm_release_slots(uint32_t start, uint32_t count) {
    if (start >= KERNEL_VM_PAGE_COUNT) return;
    if (count == 0) return;
    if (start + count > KERNEL_VM_PAGE_COUNT) count = KERNEL_VM_PAGE_COUNT - start;
    for (uint32_t i = 0; i < count; i++) {
        kernel_vm_page_table[start + i] = 0;
        kernel_vm_set_slot(start + i, 0);
        paging_invalidate_page((void*)(KERNEL_VM_WINDOW_BASE + (start + i) * PAGE_SIZE));
    }
}

static void init_low_identity_window() {
    uint32_t user_region_start = (uint32_t)__user_region_start & ~(PAGE_SIZE - 1U);
    uint32_t user_region_end = ((uint32_t)__user_region_end + PAGE_SIZE - 1U) & ~(PAGE_SIZE - 1U);

    for (uint32_t page = 0; page < LOW_IDENTITY_PAGE_COUNT; page++) {
        low_identity_page_table[page] = (page * PAGE_SIZE) | PTE_PRESENT | PDE_RW;
    }

    /* User apps now carry writable .data/.bss inside the low identity image. */
    low_identity_mark_user_range(user_region_start, user_region_end, PAGING_FLAG_WRITE);
    low_identity_mark_user_range(0x00090000U, 0x00091000U, PAGING_FLAG_WRITE);

    kernel_page_directory[0] = ((uint32_t)low_identity_page_table) | PDE_PRESENT | PDE_RW | PDE_USER;
}

static void init_kernel_stack_window() {
    uint32_t boot_stack_top_slot = KERNEL_STACK_PAGE_COUNT;
    uint32_t boot_stack_first_slot = boot_stack_top_slot - KERNEL_BOOT_STACK_PAGES;
    uint32_t boot_guard_slot = boot_stack_first_slot - KERNEL_STACK_GUARD_PAGES;

    memset(kernel_stack_page_table, 0, sizeof(kernel_stack_page_table));
    memset(kernel_stack_slot_bitmap, 0, sizeof(kernel_stack_slot_bitmap));

    for (uint32_t i = 0; i < KERNEL_BOOT_STACK_PAGES; i++) {
        uint32_t slot = boot_stack_first_slot + i;
        uint32_t phys = (KERNEL_BOOT_STACK_TOP - KERNEL_BOOT_STACK_PAGES * PAGE_SIZE) + i * PAGE_SIZE;
        kernel_stack_page_table[slot] = phys | PTE_PRESENT | PDE_RW;
        kernel_stack_set_slot(slot, 1);
    }
    for (uint32_t i = 0; i < KERNEL_STACK_GUARD_PAGES; i++) {
        kernel_stack_set_slot(boot_guard_slot + i, 1);
    }

    kernel_page_directory[KERNEL_STACK_WINDOW_PDE] = ((uint32_t)kernel_stack_page_table) | PDE_PRESENT | PDE_RW;
}

static void init_user_data_window() {
    memset(user_data_page_table, 0, sizeof(user_data_page_table));
    kernel_page_directory[USER_DATA_WINDOW_PDE] = ((uint32_t)user_data_page_table) |
                                                  PDE_PRESENT | PDE_RW | PDE_USER;
}

static void init_kernel_vm_window() {
    memset(kernel_vm_page_table, 0, sizeof(kernel_vm_page_table));
    memset(kernel_vm_slot_bitmap, 0, sizeof(kernel_vm_slot_bitmap));
    kernel_page_directory[KERNEL_VM_WINDOW_PDE] = ((uint32_t)kernel_vm_page_table) | PDE_PRESENT | PDE_RW;
}

static void reserve_range(uint64_t start, uint64_t end) {
    if (end <= start) return;
    uint32_t first = (uint32_t)(start / PAGE_SIZE);
    uint32_t last = (uint32_t)((end + PAGE_SIZE - 1ULL) / PAGE_SIZE);
    if (last > total_frames) last = total_frames;
    for (uint32_t frame = first; frame < last; frame++) {
        frame_mark(frame, 1);
    }
}

static void free_usable_ranges() {
    uint16_t entry_count = *E820_COUNT_PTR;
    e820_entry_t* entries = (e820_entry_t*)E820_DATA_PTR;
    uint64_t fallback_limit = (uint64_t)LEGACY_FALLBACK_RAM;

    memset(frame_bitmap, 0xFF, sizeof(frame_bitmap));
    total_frames = (uint32_t)(fallback_limit / PAGE_SIZE);
    used_frames = total_frames;
    managed_phys_limit = fallback_limit;

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
    if (max_addr != 0) {
        managed_phys_limit = clamp_managed_phys_limit(max_addr);
        total_frames = (uint32_t)(managed_phys_limit / PAGE_SIZE);
        if (total_frames == 0) {
            total_frames = (uint32_t)(fallback_limit / PAGE_SIZE);
            managed_phys_limit = fallback_limit;
        }
        used_frames = total_frames;
    }

    for (uint16_t i = 0; i < entry_count; i++) {
        if (entries[i].type != 1) continue;
        uint64_t region_start = entries[i].base_addr;
        uint64_t region_end = entries[i].base_addr + entries[i].length;
        if (region_end <= MIN_FRAME_ADDR) continue;
        if (region_start < MIN_FRAME_ADDR) region_start = MIN_FRAME_ADDR;
        if (region_start >= managed_phys_limit) continue;
        if (region_end > managed_phys_limit) {
            region_end = managed_phys_limit;
        }
        reserve_range(region_start, region_end);
        for (uint32_t frame = (uint32_t)(region_start / PAGE_SIZE); frame < (uint32_t)(region_end / PAGE_SIZE); frame++) {
            frame_mark(frame, 0);
        }
    }
}

static void map_large_identity_region(uint64_t start, uint64_t end, uint32_t flags) {
    uint32_t first_pde;
    uint32_t last_pde;

    if (end <= start) return;
    if (start >= MAX_MANAGED_PHYS_ADDR) return;
    if (end > MAX_MANAGED_PHYS_ADDR) end = MAX_MANAGED_PHYS_ADDR;

    first_pde = (uint32_t)(start / LARGE_PAGE_SIZE);
    last_pde = (uint32_t)((end + LARGE_PAGE_SIZE - 1ULL) / LARGE_PAGE_SIZE);
    if (last_pde > 1024U) last_pde = 1024U;
    for (uint32_t pde = first_pde; pde < last_pde; pde++) {
        kernel_page_directory[pde] = (pde * LARGE_PAGE_SIZE) | flags | PDE_PS;
    }
}

void init_paging() {
    uint64_t identity_limit;

    free_usable_ranges();
    reserve_range(0, KERNEL_RESERVED_END);

    memset(kernel_page_directory, 0, sizeof(kernel_page_directory));
    identity_limit = managed_phys_limit;
    if (identity_limit < KERNEL_RESERVED_END) identity_limit = KERNEL_RESERVED_END;
    map_large_identity_region(0, identity_limit, PDE_PRESENT | PDE_RW);
    init_low_identity_window();

    uint32_t framebuffer = *(uint32_t*)(0x6100 + 40);
    if (framebuffer != 0) {
        map_large_identity_region((uint64_t)framebuffer, (uint64_t)framebuffer + 0x00800000ULL,
                                  PDE_PRESENT | PDE_RW | PDE_USER);
    }
    init_kernel_stack_window();
    init_user_data_window();
    init_kernel_vm_window();

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

int paging_map_user_region(uint32_t virt_addr, uint32_t phys_addr, size_t size, uint32_t flags) {
    uint32_t page_count;
    uint32_t first_slot;
    uint32_t allowed_flags;

    if (size == 0) return -1;
    if ((virt_addr & (PAGE_SIZE - 1U)) != 0U) return -1;
    if ((phys_addr & (PAGE_SIZE - 1U)) != 0U) return -1;
    if (virt_addr < USER_DATA_WINDOW_BASE) return -1;
    if (virt_addr >= USER_DATA_WINDOW_BASE + USER_DATA_WINDOW_SIZE) return -1;

    page_count = (uint32_t)((size + PAGE_SIZE - 1U) / PAGE_SIZE);
    if (page_count == 0) return -1;
    if ((uint64_t)virt_addr + (uint64_t)page_count * PAGE_SIZE >
        (uint64_t)USER_DATA_WINDOW_BASE + USER_DATA_WINDOW_SIZE) return -1;
    if ((uint64_t)phys_addr + (uint64_t)page_count * PAGE_SIZE > MAX_MANAGED_PHYS_ADDR) return -1;

    first_slot = (virt_addr - USER_DATA_WINDOW_BASE) / PAGE_SIZE;
    allowed_flags = paging_allowed_pte_flags(flags) | PDE_USER;
    for (uint32_t i = 0; i < page_count; i++) {
        uint32_t virt_page = virt_addr + i * PAGE_SIZE;
        user_data_page_table[first_slot + i] = (phys_addr + i * PAGE_SIZE) | PTE_PRESENT | allowed_flags;
        paging_invalidate_page((void*)virt_page);
    }
    return 0;
}

void paging_unmap_user_region(uint32_t virt_addr, size_t size) {
    uint32_t page_count;
    uint32_t first_slot;

    if (size == 0) return;
    if ((virt_addr & (PAGE_SIZE - 1U)) != 0U) return;
    if (virt_addr < USER_DATA_WINDOW_BASE) return;
    if (virt_addr >= USER_DATA_WINDOW_BASE + USER_DATA_WINDOW_SIZE) return;

    page_count = (uint32_t)((size + PAGE_SIZE - 1U) / PAGE_SIZE);
    if (page_count == 0) return;
    if ((uint64_t)virt_addr + (uint64_t)page_count * PAGE_SIZE >
        (uint64_t)USER_DATA_WINDOW_BASE + USER_DATA_WINDOW_SIZE) return;

    first_slot = (virt_addr - USER_DATA_WINDOW_BASE) / PAGE_SIZE;
    for (uint32_t i = 0; i < page_count; i++) {
        uint32_t virt_page = virt_addr + i * PAGE_SIZE;
        user_data_page_table[first_slot + i] = 0;
        paging_invalidate_page((void*)virt_page);
    }
}

void* paging_alloc_kernel_stack(size_t stack_pages, uint32_t* out_stack_top) {
    uint32_t total_slots;
    int start_slot;
    void* phys_base;
    uint32_t stack_base;

    if (stack_pages == 0 || !out_stack_top) return 0;
    if (stack_pages + KERNEL_STACK_GUARD_PAGES > KERNEL_STACK_PAGE_COUNT) return 0;

    total_slots = (uint32_t)stack_pages + KERNEL_STACK_GUARD_PAGES;
    start_slot = kernel_stack_reserve_slots(total_slots);
    if (start_slot < 0) return 0;

    phys_base = alloc_physical_pages(stack_pages);
    if (!phys_base) {
        for (uint32_t i = 0; i < total_slots; i++) kernel_stack_set_slot((uint32_t)start_slot + i, 0);
        return 0;
    }

    for (uint32_t i = 0; i < (uint32_t)stack_pages; i++) {
        uint32_t slot = (uint32_t)start_slot + KERNEL_STACK_GUARD_PAGES + i;
        uint32_t virt = KERNEL_STACK_WINDOW_BASE + slot * PAGE_SIZE;
        kernel_stack_page_table[slot] = ((uint32_t)phys_base + i * PAGE_SIZE) | PTE_PRESENT | PDE_RW;
        paging_invalidate_page((void*)virt);
    }

    stack_base = KERNEL_STACK_WINDOW_BASE + ((uint32_t)start_slot + KERNEL_STACK_GUARD_PAGES) * PAGE_SIZE;
    *out_stack_top = stack_base + (uint32_t)stack_pages * PAGE_SIZE;
    return (void*)stack_base;
}

void* paging_map_physical(uint32_t phys_addr, size_t size, uint32_t flags) {
    uint32_t offset;
    uint32_t aligned_phys;
    uint32_t page_count;
    uint64_t span;
    uint64_t phys_end;
    int start_slot;
    uint32_t allowed_flags = paging_allowed_pte_flags(flags);

    if (size == 0) return 0;

    offset = phys_addr & (PAGE_SIZE - 1U);
    aligned_phys = phys_addr & ~(PAGE_SIZE - 1U);
    span = (uint64_t)size + (uint64_t)offset;
    page_count = (uint32_t)((span + PAGE_SIZE - 1ULL) / PAGE_SIZE);
    if (page_count == 0 || page_count > KERNEL_VM_PAGE_COUNT) return 0;

    phys_end = (uint64_t)aligned_phys + (uint64_t)page_count * PAGE_SIZE;
    if (phys_end > MAX_MANAGED_PHYS_ADDR) return 0;

    start_slot = kernel_vm_reserve_slots(page_count);
    if (start_slot < 0) return 0;

    for (uint32_t i = 0; i < page_count; i++) {
        uint32_t virt_page = KERNEL_VM_WINDOW_BASE + ((uint32_t)start_slot + i) * PAGE_SIZE;
        kernel_vm_page_table[start_slot + (int)i] = (aligned_phys + i * PAGE_SIZE) | allowed_flags | PTE_PRESENT;
        paging_invalidate_page((void*)virt_page);
    }

    return (void*)(KERNEL_VM_WINDOW_BASE + (uint32_t)start_slot * PAGE_SIZE + offset);
}

void paging_unmap_virtual(void* virt_addr, size_t size) {
    uint32_t virt;
    uint32_t offset;
    uint32_t aligned_virt;
    uint32_t start_slot;
    uint32_t page_count;
    uint64_t span;

    if (!virt_addr || size == 0) return;

    virt = (uint32_t)virt_addr;
    offset = virt & (PAGE_SIZE - 1U);
    aligned_virt = virt & ~(PAGE_SIZE - 1U);
    if (aligned_virt < KERNEL_VM_WINDOW_BASE) return;
    if (aligned_virt >= KERNEL_VM_WINDOW_BASE + KERNEL_VM_WINDOW_SIZE) return;

    span = (uint64_t)size + (uint64_t)offset;
    page_count = (uint32_t)((span + PAGE_SIZE - 1ULL) / PAGE_SIZE);
    if (page_count == 0) return;

    start_slot = (aligned_virt - KERNEL_VM_WINDOW_BASE) / PAGE_SIZE;
    kernel_vm_release_slots(start_slot, page_count);
}

uint32_t paging_kernel_stack_base() {
    return KERNEL_STACK_WINDOW_BASE;
}

uint32_t paging_kernel_stack_size() {
    return KERNEL_STACK_WINDOW_SIZE;
}

uint32_t paging_kernel_vm_base() {
    return KERNEL_VM_WINDOW_BASE;
}

uint32_t paging_kernel_vm_size() {
    return KERNEL_VM_WINDOW_SIZE;
}

uint32_t paging_total_frames() {
    return total_frames;
}

uint32_t paging_used_frames() {
    return used_frames;
}
