#include <stdint.h>

#include "ahci.h"
#include "cpu.h"
#include "gdt.h"
#include "interrupts.h"
#include "mouse.h"
#include "pci.h"
#include "rtc.h"
#include "storage.h"
#include "vbe.h"
#include "x64_paging.h"
#include "x64_serial.h"

__attribute__((used))
const uint64_t narcos_x86_64_phase7_magic = 0x4E4152434F533037ULL;

volatile uint32_t timer_ticks = 0;

void init_keyboard(void);

static void x64_panic_halt(void) {
    x64_cli();
    for (;;) {
        x64_hlt();
    }
}

static void x64_assert(int condition, const char* message) {
    if (condition) return;
    x64_serial_write("[panic] ");
    x64_serial_write_line(message);
    x64_panic_halt();
}

static void x64_write_u32(uint32_t value) {
    char buf[16];
    int index = 15;

    buf[index] = '\0';
    if (value == 0U) {
        x64_serial_write("0");
        return;
    }
    while (value != 0U && index > 0) {
        buf[--index] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    x64_serial_write(&buf[index]);
}

static int x64_wait_for_ticks(uint64_t target_ticks, uint64_t timeout_loops) {
    while (x64_timer_ticks() < target_ticks && timeout_loops-- > 0U) {
        x64_hlt();
    }
    return x64_timer_ticks() >= target_ticks ? 0 : -1;
}

static int x64_wait_for_keyboard_event(uint64_t target_count, uint64_t timeout_loops) {
    while (x64_keyboard_irq_count() < target_count && timeout_loops-- > 0U) {
        x64_hlt();
    }
    return x64_keyboard_irq_count() >= target_count ? 0 : -1;
}

static int x64_run_vm_alias_test(void) {
    uint64_t* direct;
    uint64_t* mapped;
    void* phys_page = x64_alloc_physical_page();

    if (!phys_page) return -1;

    mapped = (uint64_t*)x64_paging_map_physical((uint64_t)(uintptr_t)phys_page, 4096U, X64_PAGING_FLAG_WRITE);
    if (!mapped) {
        free_x64_physical_page(phys_page);
        return -1;
    }

    direct = (uint64_t*)phys_page;
    mapped[0] = 0x4E4152434F533637ULL;
    mapped[1] = 0xAABBCCDDEEFF0011ULL;
    if (direct[0] != 0x4E4152434F533637ULL || direct[1] != 0xAABBCCDDEEFF0011ULL) {
        x64_paging_unmap_virtual(mapped, 4096U);
        free_x64_physical_page(phys_page);
        return -1;
    }

    x64_paging_unmap_virtual(mapped, 4096U);
    free_x64_physical_page(phys_page);
    return 0;
}

static int x64_run_heap_test(void) {
    uint64_t* block_a = (uint64_t*)x64_heap_alloc(64U);
    uint64_t* block_b = (uint64_t*)x64_heap_alloc(96U);

    if (!block_a || !block_b) return -1;
    block_a[0] = 0x1111222233334444ULL;
    block_b[2] = 0x9999AAAABBBBCCCCULL;
    if (block_a[0] != 0x1111222233334444ULL || block_b[2] != 0x9999AAAABBBBCCCCULL) return -1;
    x64_heap_free(block_a);
    x64_heap_free(block_b);
    return 0;
}

static void phase7_draw_framebuffer(void) {
    uint32_t width;
    uint32_t height;
    uint32_t panel_w;

    init_vbe();
    width = vbe_get_width();
    height = vbe_get_height();
    x64_assert(width != 0U && height != 0U && vbe_get_bpp() != 0U, "framebuffer metadata missing");

    vbe_clear(0x132033);
    vbe_fill_rect(0, 0, (int)width, (int)(height / 3U), 0x1B2B44);
    vbe_fill_rect(0, (int)(height / 3U), (int)width, (int)(height / 3U), 0x254F6B);
    vbe_fill_rect(0, (int)((height / 3U) * 2U), (int)width, (int)(height - ((height / 3U) * 2U)), 0xD9E3F0);

    panel_w = width > 160U ? width - 160U : width;
    vbe_fill_rect(40, 40, (int)panel_w, (int)(height > 120U ? height - 80U : height), 0x0B1118);
    vbe_draw_rect(40, 40, (int)panel_w, (int)(height > 120U ? height - 80U : height), 0xF4C95D);
    vbe_fill_rect(72, 92, 96, 96, 0xF4C95D);
    vbe_fill_rect(192, 92, 96, 96, 0x7CC7FF);
    vbe_fill_rect(312, 92, 96, 96, 0x7FDB7F);
    vbe_fill_rect(432, 92, 96, 96, 0xF28F3B);
    vbe_update();

    x64_assert(vbe_get_pixel(80, 100) == 0xF4C95D, "framebuffer writeback mismatch");
}

static void phase7_log_pci_summary(void) {
    pci_device_info_t storage_devices[16];
    pci_bar_info_t abar;
    int total_devices;
    int storage_total;

    total_devices = pci_device_count();
    storage_total = pci_enumerate_storage(storage_devices, 16);

    x64_serial_write("[pci64] devices=");
    x64_write_u32((uint32_t)total_devices);
    x64_serial_write(" storage=");
    x64_write_u32((uint32_t)(storage_total > 0 ? storage_total : 0));
    x64_serial_write_char('\n');

    x64_assert(total_devices > 0, "pci enumeration returned no devices");
    x64_assert(storage_total > 0, "no storage controllers detected");

    if (pci_decode_bar(&storage_devices[0], 5U, &abar) == 0) {
        x64_serial_write("[pci64] storage bar5=");
        x64_serial_write_hex64(abar.base);
        x64_serial_write(" kind=");
        x64_serial_write(abar.is_io ? "io" : (abar.is_64 ? "mmio64" : "mmio32"));
        x64_serial_write_char('\n');
    }
}

static void phase7_log_rtc(void) {
    rtc_init_timezone();
    read_rtc();
    x64_serial_write("[rtc64] date=");
    x64_write_u32((uint32_t)get_day());
    x64_serial_write("/");
    x64_write_u32((uint32_t)get_month());
    x64_serial_write("/20");
    x64_write_u32((uint32_t)get_year());
    x64_serial_write(" time=");
    x64_write_u32((uint32_t)get_hour());
    x64_serial_write(":");
    x64_write_u32((uint32_t)get_minute());
    x64_serial_write(":");
    x64_write_u32((uint32_t)get_second());
    x64_serial_write_char('\n');
}

static void phase7_validate_storage(void) {
    uint8_t sector[512];

    storage_init();
    x64_assert(storage_get_backend() != STORAGE_BACKEND_NONE, "storage backend missing");
    x64_assert(storage_read_sector(0, sector) == 0, "storage sector read failed");

    x64_serial_write("[storage64] backend=");
    x64_serial_write(storage_backend_name());
    x64_serial_write(" volume_base=");
    x64_serial_write_hex32(storage_volume_base_lba());
    x64_serial_write(" partitions=");
    x64_write_u32((uint32_t)storage_partition_count());
    x64_serial_write_char('\n');
}

static void phase7_init_input(void) {
    uint64_t keyboard_before;

    init_keyboard();

    x64_outb(0xA1, (uint8_t)(x64_inb(0xA1) & (uint8_t)~(1U << 4)));
    init_mouse();

    keyboard_before = x64_keyboard_irq_count();
    x64_assert(x64_keyboard_send_echo() == 0, "keyboard echo command failed");
    x64_assert(x64_wait_for_keyboard_event(keyboard_before + 1U, 500000U) == 0, "keyboard irq did not arrive");

    x64_serial_write("[input64] keyboard_irqs=");
    x64_serial_write_hex64(x64_keyboard_irq_count());
    x64_serial_write(" mouse_irqs=");
    x64_serial_write_hex64(x64_mouse_irq_count());
    x64_serial_write_char('\n');
}

void phase7_x86_64_main(void) {
    volatile uint64_t marker = narcos_x86_64_phase7_magic;

    (void)marker;

    x64_serial_init();
    x64_serial_write_line("[X64] Phase 7 boot");

    x64_cpu_init();
    x64_gdt_init();
    x64_interrupt_init();
    x64_pic_init();
    x64_pit_init(100U);

    x64_assert(x64_paging_init() == 0, "x64 paging init failed");
    x64_assert(x64_heap_init() == 0, "x64 heap init failed");
    x64_assert(x64_run_vm_alias_test() == 0, "x64 vm alias test failed");
    x64_assert(x64_run_heap_test() == 0, "x64 heap test failed");

    phase7_draw_framebuffer();
    phase7_log_pci_summary();
    phase7_log_rtc();
    phase7_validate_storage();

    x64_sti();
    x64_assert(x64_wait_for_ticks(3U, 500000U) == 0, "timer irq did not arrive");
    phase7_init_input();

    x64_serial_write_line("[X64] Phase 7 validation OK");

    x64_cli();
    for (;;) {
        x64_hlt();
    }
}
