#include "x64_paging.h"

#include "x64_serial.h"

extern uint8_t __user_region_start[];
extern uint8_t __user_region_end[];

typedef struct {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t ext_attr;
} __attribute__((packed)) x64_e820_entry_t;

typedef struct x64_heap_block {
    size_t size;
    uint8_t is_free;
    struct x64_heap_block* next;
} x64_heap_block_t;

#define X64_PAGE_SIZE               4096ULL
#define X64_LARGE_PAGE_SIZE         0x200000ULL
#define X64_LEGACY_FALLBACK_RAM     (64ULL * 1024ULL * 1024ULL)
#define X64_MAX_MANAGED_PHYS_ADDR   0x100000000ULL
#define X64_MAX_FRAMES              (X64_MAX_MANAGED_PHYS_ADDR / X64_PAGE_SIZE)
#define X64_BITMAP_SIZE             (X64_MAX_FRAMES / 8ULL)
#define X64_RESERVED_PHYS_END       0x00400000ULL
#define X64_HEAP_SIZE               (8ULL * 1024ULL * 1024ULL)
#define X64_VM_WINDOW_SIZE          (16ULL * 1024ULL * 1024ULL)
#define X64_HEAP_BASE               0xFFFF800000200000ULL
#define X64_VM_WINDOW_BASE          0xFFFF800000A00000ULL
#define X64_HEAP_PAGE_COUNT         (X64_HEAP_SIZE / X64_PAGE_SIZE)
#define X64_VM_WINDOW_PAGE_COUNT    (X64_VM_WINDOW_SIZE / X64_PAGE_SIZE)
#define X64_VM_WINDOW_PT_COUNT      (X64_VM_WINDOW_SIZE / X64_LARGE_PAGE_SIZE)
#define X64_HEAP_PT_COUNT           (X64_HEAP_SIZE / X64_LARGE_PAGE_SIZE)
#define X64_USER_WINDOW_BASE        0x0000000040000000ULL
#define X64_USER_WINDOW_SIZE        0x0000000000800000ULL
#define X64_USER_WINDOW_PT_COUNT    (X64_USER_WINDOW_SIZE / X64_LARGE_PAGE_SIZE)
#define X64_USER_WINDOW_PAGE_COUNT  (X64_USER_WINDOW_SIZE / X64_PAGE_SIZE)
#define X64_IDENTITY_PDPT_COUNT     4U
#define X64_PAGING_PRESENT          0x001ULL
#define X64_PAGING_RW               0x002ULL
#define X64_PAGING_USER             0x004ULL
#define X64_PAGING_PWT              0x008ULL
#define X64_PAGING_PCD              0x010ULL
#define X64_PAGING_PS               0x080ULL

#define X64_E820_COUNT_PTR ((uint16_t*)0x5000)
#define X64_E820_DATA_PTR  ((uint8_t*)0x5002)

#define X64_PML4_INDEX(addr) (((uint64_t)(addr) >> 39) & 0x1FFULL)
#define X64_PDPT_INDEX(addr) (((uint64_t)(addr) >> 30) & 0x1FFULL)
#define X64_PD_INDEX(addr)   (((uint64_t)(addr) >> 21) & 0x1FFULL)
#define X64_PT_INDEX(addr)   (((uint64_t)(addr) >> 12) & 0x1FFULL)

static uint64_t kernel_pml4[512] __attribute__((aligned(4096)));
static uint64_t identity_pdpt[512] __attribute__((aligned(4096)));
static uint64_t identity_pds[X64_IDENTITY_PDPT_COUNT][512] __attribute__((aligned(4096)));
static uint64_t low_identity_pt0[512] __attribute__((aligned(4096)));
static uint64_t high_pdpt[512] __attribute__((aligned(4096)));
static uint64_t high_pd[512] __attribute__((aligned(4096)));
static uint64_t heap_pts[X64_HEAP_PT_COUNT][512] __attribute__((aligned(4096)));
static uint64_t vm_pts[X64_VM_WINDOW_PT_COUNT][512] __attribute__((aligned(4096)));
static uint64_t user_pts[X64_USER_WINDOW_PT_COUNT][512] __attribute__((aligned(4096)));
static uint8_t frame_bitmap[X64_BITMAP_SIZE];
static uint8_t vm_slot_bitmap[X64_VM_WINDOW_PAGE_COUNT / 8ULL];

static uint64_t total_frames = 0;
static uint64_t used_frames = 0;
static uint64_t managed_phys_limit = 0;
static x64_heap_block_t* heap_head = 0;

static uint64_t x64_clamp_managed_phys_limit(uint64_t addr) {
    if (addr > X64_MAX_MANAGED_PHYS_ADDR) return X64_MAX_MANAGED_PHYS_ADDR;
    return addr;
}

static void x64_invalidate_page(void* addr) {
    __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

static void x64_load_page_root(uint64_t* root) {
    uint64_t value = (uint64_t)(uintptr_t)root;
    __asm__ volatile("mov %0, %%cr3" : : "r"(value) : "memory");
}

static uint64_t x64_allowed_pte_flags(uint64_t flags) {
    return flags & (X64_PAGING_FLAG_WRITE | X64_PAGING_FLAG_USER |
                    X64_PAGING_FLAG_WRITE_THROUGH | X64_PAGING_FLAG_CACHE_DISABLE);
}

static int x64_vm_slot_used(uint64_t slot) {
    uint8_t mask = (uint8_t)(1U << (slot & 7ULL));
    return (vm_slot_bitmap[slot >> 3ULL] & mask) != 0U;
}

static void x64_vm_set_slot(uint64_t slot, int used) {
    uint8_t mask = (uint8_t)(1U << (slot & 7ULL));
    if (used) vm_slot_bitmap[slot >> 3ULL] |= mask;
    else vm_slot_bitmap[slot >> 3ULL] &= (uint8_t)~mask;
}

static int64_t x64_vm_reserve_slots(uint64_t count) {
    uint64_t run = 0;
    uint64_t start = 0;

    if (count == 0 || count > X64_VM_WINDOW_PAGE_COUNT) return -1;
    for (uint64_t slot = 0; slot < X64_VM_WINDOW_PAGE_COUNT; slot++) {
        if (!x64_vm_slot_used(slot)) {
            if (run == 0) start = slot;
            run++;
            if (run == count) {
                for (uint64_t i = 0; i < count; i++) x64_vm_set_slot(start + i, 1);
                return (int64_t)start;
            }
        } else {
            run = 0;
        }
    }
    return -1;
}

static void x64_vm_release_slots(uint64_t start, uint64_t count) {
    if (count == 0 || start >= X64_VM_WINDOW_PAGE_COUNT) return;
    if (start + count > X64_VM_WINDOW_PAGE_COUNT) count = X64_VM_WINDOW_PAGE_COUNT - start;

    for (uint64_t i = 0; i < count; i++) {
        uint64_t slot = start + i;
        uint64_t pt_index = slot / 512ULL;
        uint64_t page_index = slot % 512ULL;
        uint64_t virt = X64_VM_WINDOW_BASE + slot * X64_PAGE_SIZE;

        vm_pts[pt_index][page_index] = 0;
        x64_vm_set_slot(slot, 0);
        x64_invalidate_page((void*)(uintptr_t)virt);
    }
}

static void x64_frame_mark(uint64_t frame, int used) {
    uint8_t mask;
    uint8_t* slot;
    int was_used;

    if (frame >= X64_MAX_FRAMES) return;
    mask = (uint8_t)(1U << (frame & 7ULL));
    slot = &frame_bitmap[frame >> 3ULL];
    was_used = (*slot & mask) != 0U;

    if (used && !was_used) {
        *slot |= mask;
        used_frames++;
    } else if (!used && was_used) {
        *slot &= (uint8_t)~mask;
        used_frames--;
    }
}

static void x64_reserve_range(uint64_t start, uint64_t end) {
    uint64_t first;
    uint64_t last;

    if (end <= start) return;
    first = start / X64_PAGE_SIZE;
    last = (end + X64_PAGE_SIZE - 1ULL) / X64_PAGE_SIZE;
    if (last > total_frames) last = total_frames;
    for (uint64_t frame = first; frame < last; frame++) {
        x64_frame_mark(frame, 1);
    }
}

static void x64_free_usable_ranges(void) {
    uint16_t entry_count = *X64_E820_COUNT_PTR;
    x64_e820_entry_t* entries = (x64_e820_entry_t*)X64_E820_DATA_PTR;
    uint64_t fallback_limit = X64_LEGACY_FALLBACK_RAM;

    for (uint64_t i = 0; i < X64_BITMAP_SIZE; i++) frame_bitmap[i] = 0xFFU;
    total_frames = fallback_limit / X64_PAGE_SIZE;
    used_frames = total_frames;
    managed_phys_limit = fallback_limit;

    if (entry_count == 0U) {
        for (uint64_t frame = X64_RESERVED_PHYS_END / X64_PAGE_SIZE; frame < total_frames; frame++) {
            x64_frame_mark(frame, 0);
        }
        return;
    }

    {
        uint64_t max_addr = 0;
        for (uint16_t i = 0; i < entry_count; i++) {
            uint64_t region_end;

            if (entries[i].type != 1U) continue;
            region_end = entries[i].base_addr + entries[i].length;
            if (region_end > max_addr) max_addr = region_end;
        }

        if (max_addr != 0ULL) {
            managed_phys_limit = x64_clamp_managed_phys_limit(max_addr);
            total_frames = managed_phys_limit / X64_PAGE_SIZE;
            if (total_frames == 0ULL) {
                total_frames = fallback_limit / X64_PAGE_SIZE;
                managed_phys_limit = fallback_limit;
            }
            used_frames = total_frames;
        }
    }

    for (uint16_t i = 0; i < entry_count; i++) {
        uint64_t region_start;
        uint64_t region_end;

        if (entries[i].type != 1U) continue;

        region_start = entries[i].base_addr;
        region_end = entries[i].base_addr + entries[i].length;
        if (region_end <= X64_RESERVED_PHYS_END) continue;
        if (region_start < X64_RESERVED_PHYS_END) region_start = X64_RESERVED_PHYS_END;
        if (region_start >= managed_phys_limit) continue;
        if (region_end > managed_phys_limit) region_end = managed_phys_limit;

        x64_reserve_range(region_start, region_end);
        for (uint64_t frame = region_start / X64_PAGE_SIZE; frame < region_end / X64_PAGE_SIZE; frame++) {
            x64_frame_mark(frame, 0);
        }
    }
}

static void x64_setup_identity_map(uint64_t limit) {
    uint64_t rounded_limit = (limit + X64_LARGE_PAGE_SIZE - 1ULL) & ~(X64_LARGE_PAGE_SIZE - 1ULL);
    uint64_t page_addr = 0;

    if (rounded_limit > X64_MAX_MANAGED_PHYS_ADDR) rounded_limit = X64_MAX_MANAGED_PHYS_ADDR;

    for (uint64_t pdpt_index = 0; pdpt_index < X64_IDENTITY_PDPT_COUNT; pdpt_index++) {
        identity_pdpt[pdpt_index] = ((uint64_t)(uintptr_t)&identity_pds[pdpt_index][0]) |
                                    X64_PAGING_PRESENT | X64_PAGING_RW;
        for (uint64_t pd_index = 0; pd_index < 512ULL; pd_index++) {
            if (page_addr < rounded_limit) {
                identity_pds[pdpt_index][pd_index] = page_addr |
                                                     X64_PAGING_PRESENT |
                                                     X64_PAGING_RW |
                                                     X64_PAGING_PS;
            } else {
                identity_pds[pdpt_index][pd_index] = 0;
            }
            page_addr += X64_LARGE_PAGE_SIZE;
        }
    }

    kernel_pml4[0] = ((uint64_t)(uintptr_t)identity_pdpt) | X64_PAGING_PRESENT | X64_PAGING_RW;
}

static void x64_split_low_identity_window(void) {
    for (uint64_t page = 0; page < 512ULL; page++) {
        low_identity_pt0[page] = (page * X64_PAGE_SIZE) | X64_PAGING_PRESENT | X64_PAGING_RW;
    }
    identity_pds[0][0] = ((uint64_t)(uintptr_t)low_identity_pt0) | X64_PAGING_PRESENT | X64_PAGING_RW;
}

static int x64_mark_low_identity_user_range(uint64_t start, uint64_t end, uint64_t flags) {
    uint64_t first_page;
    uint64_t last_page;
    uint64_t allowed_flags;

    if (end <= start) return 0;
    if (end > X64_LARGE_PAGE_SIZE) return -1;

    kernel_pml4[0] |= X64_PAGING_USER;
    identity_pdpt[0] |= X64_PAGING_USER;
    identity_pds[0][0] |= X64_PAGING_USER;

    first_page = start / X64_PAGE_SIZE;
    last_page = (end + X64_PAGE_SIZE - 1ULL) / X64_PAGE_SIZE;
    if (last_page > 512ULL) last_page = 512ULL;
    allowed_flags = x64_allowed_pte_flags(flags) | X64_PAGING_USER;

    for (uint64_t page = first_page; page < last_page; page++) {
        low_identity_pt0[page] |= allowed_flags;
    }
    return 0;
}

static void x64_setup_high_windows(void) {
    uint64_t heap_pd_index = X64_PD_INDEX(X64_HEAP_BASE);
    uint64_t vm_pd_index = X64_PD_INDEX(X64_VM_WINDOW_BASE);
    uint64_t high_pml4_index = X64_PML4_INDEX(X64_HEAP_BASE);
    uint64_t high_pdpt_index = X64_PDPT_INDEX(X64_HEAP_BASE);

    kernel_pml4[high_pml4_index] = ((uint64_t)(uintptr_t)high_pdpt) | X64_PAGING_PRESENT | X64_PAGING_RW;
    high_pdpt[high_pdpt_index] = ((uint64_t)(uintptr_t)high_pd) | X64_PAGING_PRESENT | X64_PAGING_RW;

    for (uint64_t i = 0; i < X64_HEAP_PT_COUNT; i++) {
        high_pd[heap_pd_index + i] = ((uint64_t)(uintptr_t)heap_pts[i]) | X64_PAGING_PRESENT | X64_PAGING_RW;
    }
    for (uint64_t i = 0; i < X64_VM_WINDOW_PT_COUNT; i++) {
        high_pd[vm_pd_index + i] = ((uint64_t)(uintptr_t)vm_pts[i]) | X64_PAGING_PRESENT | X64_PAGING_RW;
    }
}

static void x64_setup_user_window(void) {
    uint64_t user_pml4_index = X64_PML4_INDEX(X64_USER_WINDOW_BASE);
    uint64_t user_pdpt_index = X64_PDPT_INDEX(X64_USER_WINDOW_BASE);
    uint64_t user_pd_index = X64_PD_INDEX(X64_USER_WINDOW_BASE);

    kernel_pml4[user_pml4_index] |= X64_PAGING_USER;
    identity_pdpt[user_pdpt_index] |= X64_PAGING_USER;
    for (uint64_t i = 0; i < X64_USER_WINDOW_PT_COUNT; i++) {
        identity_pds[user_pdpt_index][user_pd_index + i] =
            ((uint64_t)(uintptr_t)user_pts[i]) | X64_PAGING_PRESENT | X64_PAGING_RW | X64_PAGING_USER;
    }
}

static int x64_map_heap_pages(void) {
    for (uint64_t i = 0; i < X64_HEAP_PAGE_COUNT; i++) {
        void* phys_page = x64_alloc_physical_page();
        uint64_t pt_index;
        uint64_t page_index;

        if (!phys_page) return -1;
        pt_index = i / 512ULL;
        page_index = i % 512ULL;
        heap_pts[pt_index][page_index] = ((uint64_t)(uintptr_t)phys_page) | X64_PAGING_PRESENT | X64_PAGING_RW;
    }
    return 0;
}

int x64_paging_init(void) {
    uint64_t identity_limit;

    for (uint64_t i = 0; i < 512ULL; i++) {
        kernel_pml4[i] = 0;
        identity_pdpt[i] = 0;
        low_identity_pt0[i] = 0;
        high_pdpt[i] = 0;
        high_pd[i] = 0;
        }
    for (uint64_t pt = 0; pt < X64_HEAP_PT_COUNT; pt++) {
        for (uint64_t i = 0; i < 512ULL; i++) heap_pts[pt][i] = 0;
    }
    for (uint64_t pdpt = 0; pdpt < X64_IDENTITY_PDPT_COUNT; pdpt++) {
        for (uint64_t i = 0; i < 512ULL; i++) identity_pds[pdpt][i] = 0;
    }
    for (uint64_t pt = 0; pt < X64_VM_WINDOW_PT_COUNT; pt++) {
        for (uint64_t i = 0; i < 512ULL; i++) vm_pts[pt][i] = 0;
    }
    for (uint64_t pt = 0; pt < X64_USER_WINDOW_PT_COUNT; pt++) {
        for (uint64_t i = 0; i < 512ULL; i++) user_pts[pt][i] = 0;
    }
    for (uint64_t i = 0; i < sizeof(vm_slot_bitmap); i++) vm_slot_bitmap[i] = 0;

    x64_free_usable_ranges();
    x64_reserve_range(0, X64_RESERVED_PHYS_END);

    identity_limit = managed_phys_limit;
    if (identity_limit < X64_RESERVED_PHYS_END) identity_limit = X64_RESERVED_PHYS_END;

    x64_setup_identity_map(identity_limit);
    x64_split_low_identity_window();
    if (x64_mark_low_identity_user_range((uint64_t)(uintptr_t)__user_region_start,
                                         (uint64_t)(uintptr_t)__user_region_end,
                                         X64_PAGING_FLAG_WRITE) != 0) {
        x64_serial_write_line("[paging64] user code region exceeds split low window");
        return -1;
    }
    x64_setup_high_windows();
    x64_setup_user_window();
    if (x64_map_heap_pages() != 0) {
        x64_serial_write_line("[paging64] heap backing allocation failed");
        return -1;
    }

    x64_load_page_root(kernel_pml4);
    x64_serial_write("[paging64] identity_limit=");
    x64_serial_write_hex64(identity_limit);
    x64_serial_write(" heap_base=");
    x64_serial_write_hex64(X64_HEAP_BASE);
    x64_serial_write(" vm_base=");
    x64_serial_write_hex64(X64_VM_WINDOW_BASE);
    x64_serial_write_char('\n');

    return 0;
}

void* x64_alloc_physical_pages(size_t count) {
    uint64_t needed;
    uint64_t run = 0;
    uint64_t start = 0;
    uint64_t min_frame = X64_RESERVED_PHYS_END / X64_PAGE_SIZE;

    if (count == 0 || total_frames == 0) return 0;
    needed = (uint64_t)count;
    if (min_frame >= total_frames) return 0;

    for (uint64_t frame = min_frame; frame < total_frames; frame++) {
        uint8_t mask = (uint8_t)(1U << (frame & 7ULL));
        if ((frame_bitmap[frame >> 3ULL] & mask) == 0U) {
            if (run == 0) start = frame;
            run++;
            if (run == needed) {
                for (uint64_t i = 0; i < needed; i++) x64_frame_mark(start + i, 1);
                return (void*)(uintptr_t)(start * X64_PAGE_SIZE);
            }
        } else {
            run = 0;
        }
    }

    return 0;
}

void* x64_alloc_physical_page(void) {
    return x64_alloc_physical_pages(1);
}

void free_x64_physical_pages(void* base, size_t count) {
    uint64_t start;
    uint64_t end;

    if (!base || count == 0) return;
    start = (uint64_t)(uintptr_t)base / X64_PAGE_SIZE;
    end = start + (uint64_t)count;
    if (end > total_frames) end = total_frames;
    for (uint64_t frame = start; frame < end; frame++) {
        if (frame >= X64_RESERVED_PHYS_END / X64_PAGE_SIZE) x64_frame_mark(frame, 0);
    }
}

void free_x64_physical_page(void* page) {
    free_x64_physical_pages(page, 1);
}

void* x64_paging_map_physical(uint64_t phys_addr, size_t size, uint64_t flags) {
    uint64_t aligned_phys;
    uint64_t offset;
    uint64_t span;
    uint64_t page_count;
    int64_t start_slot;
    uint64_t allowed_flags;

    if (size == 0 || phys_addr >= X64_MAX_MANAGED_PHYS_ADDR) return 0;

    aligned_phys = phys_addr & ~(X64_PAGE_SIZE - 1ULL);
    offset = phys_addr - aligned_phys;
    span = offset + (uint64_t)size;
    page_count = (span + X64_PAGE_SIZE - 1ULL) / X64_PAGE_SIZE;
    if (page_count == 0 || aligned_phys + page_count * X64_PAGE_SIZE > X64_MAX_MANAGED_PHYS_ADDR) return 0;

    start_slot = x64_vm_reserve_slots(page_count);
    if (start_slot < 0) return 0;

    allowed_flags = x64_allowed_pte_flags(flags) | X64_PAGING_PRESENT;
    for (uint64_t i = 0; i < page_count; i++) {
        uint64_t slot = (uint64_t)start_slot + i;
        uint64_t pt_index = slot / 512ULL;
        uint64_t page_index = slot % 512ULL;
        uint64_t virt = X64_VM_WINDOW_BASE + slot * X64_PAGE_SIZE;

        vm_pts[pt_index][page_index] = (aligned_phys + i * X64_PAGE_SIZE) | allowed_flags;
        x64_invalidate_page((void*)(uintptr_t)virt);
    }

    return (void*)(uintptr_t)(X64_VM_WINDOW_BASE + (uint64_t)start_slot * X64_PAGE_SIZE + offset);
}

void x64_paging_unmap_virtual(void* virt_addr, size_t size) {
    uint64_t virt;
    uint64_t aligned_virt;
    uint64_t offset;
    uint64_t span;
    uint64_t page_count;
    uint64_t start_slot;

    if (!virt_addr || size == 0) return;
    virt = (uint64_t)(uintptr_t)virt_addr;
    if (virt < X64_VM_WINDOW_BASE || virt >= X64_VM_WINDOW_BASE + X64_VM_WINDOW_SIZE) return;

    aligned_virt = virt & ~(X64_PAGE_SIZE - 1ULL);
    offset = virt - aligned_virt;
    span = offset + (uint64_t)size;
    page_count = (span + X64_PAGE_SIZE - 1ULL) / X64_PAGE_SIZE;
    start_slot = (aligned_virt - X64_VM_WINDOW_BASE) / X64_PAGE_SIZE;
    x64_vm_release_slots(start_slot, page_count);
}

int x64_paging_map_user_region(uint64_t virt_addr, uint64_t phys_addr, size_t size, uint64_t flags) {
    uint64_t page_count;
    uint64_t first_slot;
    uint64_t allowed_flags;

    if (size == 0) return -1;
    if ((virt_addr & (X64_PAGE_SIZE - 1ULL)) != 0ULL) return -1;
    if ((phys_addr & (X64_PAGE_SIZE - 1ULL)) != 0ULL) return -1;
    if (virt_addr < X64_USER_WINDOW_BASE) return -1;
    if (virt_addr >= X64_USER_WINDOW_BASE + X64_USER_WINDOW_SIZE) return -1;

    page_count = ((uint64_t)size + X64_PAGE_SIZE - 1ULL) / X64_PAGE_SIZE;
    if (page_count == 0ULL) return -1;
    if (virt_addr + page_count * X64_PAGE_SIZE > X64_USER_WINDOW_BASE + X64_USER_WINDOW_SIZE) return -1;
    if (phys_addr + page_count * X64_PAGE_SIZE > X64_MAX_MANAGED_PHYS_ADDR) return -1;

    first_slot = (virt_addr - X64_USER_WINDOW_BASE) / X64_PAGE_SIZE;
    allowed_flags = x64_allowed_pte_flags(flags) | X64_PAGING_PRESENT | X64_PAGING_USER;
    for (uint64_t i = 0; i < page_count; i++) {
        uint64_t slot = first_slot + i;
        uint64_t pt_index = slot / 512ULL;
        uint64_t page_index = slot % 512ULL;
        uint64_t virt_page = virt_addr + i * X64_PAGE_SIZE;

        user_pts[pt_index][page_index] = (phys_addr + i * X64_PAGE_SIZE) | allowed_flags;
        x64_invalidate_page((void*)(uintptr_t)virt_page);
    }
    return 0;
}

void x64_paging_unmap_user_region(uint64_t virt_addr, size_t size) {
    uint64_t page_count;
    uint64_t first_slot;

    if (size == 0) return;
    if ((virt_addr & (X64_PAGE_SIZE - 1ULL)) != 0ULL) return;
    if (virt_addr < X64_USER_WINDOW_BASE) return;
    if (virt_addr >= X64_USER_WINDOW_BASE + X64_USER_WINDOW_SIZE) return;

    page_count = ((uint64_t)size + X64_PAGE_SIZE - 1ULL) / X64_PAGE_SIZE;
    if (page_count == 0ULL) return;
    if (virt_addr + page_count * X64_PAGE_SIZE > X64_USER_WINDOW_BASE + X64_USER_WINDOW_SIZE) return;

    first_slot = (virt_addr - X64_USER_WINDOW_BASE) / X64_PAGE_SIZE;
    for (uint64_t i = 0; i < page_count; i++) {
        uint64_t slot = first_slot + i;
        uint64_t pt_index = slot / 512ULL;
        uint64_t page_index = slot % 512ULL;
        uint64_t virt_page = virt_addr + i * X64_PAGE_SIZE;

        user_pts[pt_index][page_index] = 0;
        x64_invalidate_page((void*)(uintptr_t)virt_page);
    }
}

int x64_heap_init(void) {
    uintptr_t heap_start = (uintptr_t)X64_HEAP_BASE;

    heap_start = (heap_start + 15ULL) & ~(uintptr_t)15ULL;
    heap_head = (x64_heap_block_t*)heap_start;
    heap_head->size = (size_t)(X64_HEAP_SIZE - sizeof(x64_heap_block_t));
    heap_head->is_free = 1U;
    heap_head->next = 0;
    return 0;
}

void* x64_heap_alloc(size_t size) {
    x64_heap_block_t* curr;
    size_t aligned_size;

    if (size == 0 || !heap_head) return 0;
    aligned_size = (size + 15U) & ~(size_t)15U;
    curr = heap_head;

    while (curr != 0) {
        if (curr->is_free && curr->size >= aligned_size) {
            if (curr->size > aligned_size + sizeof(x64_heap_block_t) + 16U) {
                x64_heap_block_t* new_block =
                    (x64_heap_block_t*)((uintptr_t)curr + sizeof(x64_heap_block_t) + aligned_size);
                new_block->size = curr->size - aligned_size - sizeof(x64_heap_block_t);
                new_block->is_free = 1U;
                new_block->next = curr->next;
                curr->size = aligned_size;
                curr->next = new_block;
            }
            curr->is_free = 0U;
            return (void*)((uintptr_t)curr + sizeof(x64_heap_block_t));
        }
        curr = curr->next;
    }

    return 0;
}

void x64_heap_free(void* ptr) {
    x64_heap_block_t* block;
    x64_heap_block_t* curr;

    if (!ptr || !heap_head) return;
    block = (x64_heap_block_t*)((uintptr_t)ptr - sizeof(x64_heap_block_t));
    block->is_free = 1U;

    curr = heap_head;
    while (curr != 0) {
        if (curr->is_free && curr->next != 0 && curr->next->is_free) {
            curr->size += sizeof(x64_heap_block_t) + curr->next->size;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
}

uint64_t x64_paging_heap_base(void) {
    return X64_HEAP_BASE;
}

uint64_t x64_paging_heap_size(void) {
    return X64_HEAP_SIZE;
}

uint64_t x64_paging_kernel_vm_base(void) {
    return X64_VM_WINDOW_BASE;
}

uint64_t x64_paging_kernel_vm_size(void) {
    return X64_VM_WINDOW_SIZE;
}

uint64_t x64_paging_total_frames(void) {
    return total_frames;
}

uint64_t x64_paging_used_frames(void) {
    return used_frames;
}
