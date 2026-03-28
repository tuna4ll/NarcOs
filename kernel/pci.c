#include "pci.h"
#include "gdt.h"
#include "io.h"
#include "string.h"

extern void vga_print(const char* str);
extern void vga_println(const char* str);
extern void vga_print_int(int num);

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC
#define PIC1_DATA          0x21
#define PIC2_DATA          0xA1

static const char* pci_class_name(uint8_t class_code, uint8_t subclass) {
    switch (class_code) {
        case 0x01:
            if (subclass == 0x01) return "IDE Storage";
            if (subclass == 0x06) return "SATA Storage";
            if (subclass == 0x08) return "NVM Storage";
            return "Storage";
        case 0x02: return "Network";
        case 0x03: return "Display";
        case 0x04: return "Multimedia";
        case 0x05: return "Memory";
        case 0x06:
            if (subclass == 0x00) return "Host Bridge";
            if (subclass == 0x01) return "ISA Bridge";
            if (subclass == 0x04) return "PCI Bridge";
            return "Bridge";
        case 0x07: return "Communication";
        case 0x08: return "System Peripheral";
        case 0x09: return "Input";
        case 0x0C:
            if (subclass == 0x03) return "USB Controller";
            return "Serial Bus";
        default: return "Unknown";
    }
}

const char* pci_irq_pin_name(uint8_t pin) {
    switch (pin) {
        case 1: return "INTA#";
        case 2: return "INTB#";
        case 3: return "INTC#";
        case 4: return "INTD#";
        default: return "INT?#";
    }
}

const char* pci_storage_controller_name(const pci_device_info_t* dev) {
    if (!dev || dev->class_code != 0x01) return "Not Storage";

    switch (dev->subclass) {
        case 0x00: return "SCSI Storage Controller";
        case 0x01:
            if ((dev->prog_if & 0x80U) != 0U) return "IDE Controller (Bus Mastering)";
            return "IDE Controller";
        case 0x04: return "RAID Storage Controller";
        case 0x05: return "ATA Controller";
        case 0x06:
            if (dev->prog_if == 0x01U) return "AHCI SATA Controller";
            if (dev->prog_if == 0x02U) return "Serial Storage Bus SATA";
            return "SATA Controller";
        case 0x07: return "SAS Controller";
        case 0x08:
            if (dev->prog_if == 0x02U) return "NVMe Controller";
            return "NVM Storage Controller";
        case 0x80: return "Other Storage Controller";
        default: return "Storage Controller";
    }
}

static void pci_print_hex16(uint16_t value) {
    char buf[11];
    vga_print_int_hex((uint32_t)value, buf);
    vga_print(buf + 6);
}

static void pci_print_hex8(uint8_t value) {
    char buf[11];
    vga_print_int_hex((uint32_t)value, buf);
    vga_print(buf + 8);
}

static void pci_print_hex32(uint32_t value) {
    char buf[11];
    vga_print_int_hex(value, buf);
    vga_print(buf);
}

static uint32_t pci_device_bar_value(const pci_device_info_t* dev, uint8_t bar_index) {
    switch (bar_index) {
        case 0: return dev->bar0;
        case 1: return dev->bar1;
        case 2: return dev->bar2;
        case 3: return dev->bar3;
        case 4: return dev->bar4;
        case 5: return dev->bar5;
        default: return 0;
    }
}

static int pci_legacy_irq_read_masked(uint8_t irq_line, uint8_t* out_masked) {
    uint16_t port;
    uint8_t bit;
    uint8_t mask;

    if (!out_masked || irq_line >= 16U || irq_line == 2U) return -1;
    port = irq_line < 8U ? PIC1_DATA : PIC2_DATA;
    bit = irq_line < 8U ? irq_line : (uint8_t)(irq_line - 8U);
    mask = inb(port);
    *out_masked = (uint8_t)(((mask >> bit) & 0x1U) != 0U);
    return 0;
}

static int pci_legacy_irq_set_masked(uint8_t irq_line, int masked) {
    uint16_t port;
    uint8_t bit;
    uint8_t mask;

    if (irq_line >= 16U || irq_line == 2U) return -1;
    port = irq_line < 8U ? PIC1_DATA : PIC2_DATA;
    bit = irq_line < 8U ? irq_line : (uint8_t)(irq_line - 8U);
    mask = inb(port);
    if (masked) mask |= (uint8_t)(1U << bit);
    else mask &= (uint8_t)~(1U << bit);
    outb(port, mask);
    return 0;
}

static void pci_print_irq_short(const pci_irq_route_t* route) {
    if (!route || !route->present) return;

    vga_print("  irq ");
    vga_print(pci_irq_pin_name(route->irq_pin));
    vga_print("->");
    if (route->routed) {
        vga_print_int(route->irq_line);
    } else {
        vga_print("unrouted");
    }
}

static void pci_print_irq_detail_line(const pci_irq_route_t* route) {
    if (!route || !route->present) return;

    vga_print("    IRQ ");
    vga_print(pci_irq_pin_name(route->irq_pin));
    if (route->routed) {
        vga_print(" -> ");
        vga_print_int(route->irq_line);
        vga_println(route->masked ? " (masked)" : " (enabled)");
    } else {
        vga_println(" -> unrouted");
    }
}

static void pci_fill_device_info(pci_device_info_t* dev, uint8_t bus, uint8_t slot, uint8_t func,
                                 uint16_t vendor, uint16_t device, uint32_t class_reg, uint32_t header_reg) {
    uint32_t irq_reg;

    dev->bus = bus;
    dev->slot = slot;
    dev->func = func;
    dev->vendor_id = vendor;
    dev->device_id = device;
    dev->revision = (uint8_t)(class_reg & 0xFFU);
    dev->prog_if = (uint8_t)((class_reg >> 8) & 0xFFU);
    dev->subclass = (uint8_t)((class_reg >> 16) & 0xFFU);
    dev->class_code = (uint8_t)((class_reg >> 24) & 0xFFU);
    dev->header_type = (uint8_t)((header_reg >> 16) & 0xFFU);
    irq_reg = pci_read32(bus, slot, func, 0x3C);
    dev->interrupt_line = (uint8_t)(irq_reg & 0xFFU);
    dev->interrupt_pin = (uint8_t)((irq_reg >> 8) & 0xFFU);
    dev->bar0 = pci_read32(bus, slot, func, 0x10);
    dev->bar1 = pci_read32(bus, slot, func, 0x14);
    dev->bar2 = pci_read32(bus, slot, func, 0x18);
    dev->bar3 = pci_read32(bus, slot, func, 0x1C);
    dev->bar4 = pci_read32(bus, slot, func, 0x20);
    dev->bar5 = pci_read32(bus, slot, func, 0x24);
}

static int pci_enumerate_internal(pci_device_info_t* out_devices, int max_devices, int storage_only) {
    int count = 0;

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t vendor_device = pci_read32((uint8_t)bus, slot, func, 0x00);
                uint16_t vendor = (uint16_t)(vendor_device & 0xFFFFU);
                uint16_t device = (uint16_t)((vendor_device >> 16) & 0xFFFFU);
                uint32_t class_reg;
                uint32_t header_reg;

                if (vendor == 0xFFFFU) {
                    if (func == 0) break;
                    continue;
                }

                class_reg = pci_read32((uint8_t)bus, slot, func, 0x08);
                if (storage_only && ((class_reg >> 24) & 0xFFU) != 0x01U) continue;
                header_reg = pci_read32((uint8_t)bus, slot, func, 0x0C);

                if (out_devices && count < max_devices) {
                    pci_fill_device_info(&out_devices[count], (uint8_t)bus, slot, func,
                                         vendor, device, class_reg, header_reg);
                }

                count++;
            }
        }
    }

    return count;
}

static void pci_print_bar_line(const char* prefix, uint32_t low, uint32_t high) {
    if (low == 0U || low == 0xFFFFFFFFU) return;

    vga_print(prefix);
    if ((low & 0x1U) != 0U) {
        vga_print("IO ");
        pci_print_hex32(low & ~0x3U);
    } else if ((low & 0x6U) == 0x4U) {
        vga_print("MMIO64 ");
        pci_print_hex32(high);
        vga_print(":");
        pci_print_hex32(low & ~0xFU);
    } else {
        vga_print("MMIO ");
        pci_print_hex32(low & ~0xFU);
    }
    vga_println("");
}

int pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_info_t* out_device) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t vendor_device = pci_read32((uint8_t)bus, slot, func, 0x00);
                uint16_t vendor = (uint16_t)(vendor_device & 0xFFFFU);
                uint16_t device = (uint16_t)((vendor_device >> 16) & 0xFFFFU);
                uint32_t class_reg;
                uint32_t header_reg;

                if (vendor == 0xFFFFU) {
                    if (func == 0) break;
                    continue;
                }
                if (vendor != vendor_id || device != device_id) continue;

                if (out_device) {
                    class_reg = pci_read32((uint8_t)bus, slot, func, 0x08);
                    header_reg = pci_read32((uint8_t)bus, slot, func, 0x0C);
                    pci_fill_device_info(out_device, (uint8_t)bus, slot, func,
                                         vendor, device, class_reg, header_reg);
                }
                return 0;
            }
        }
    }
    return -1;
}

int pci_decode_bar(const pci_device_info_t* dev, uint8_t bar_index, pci_bar_info_t* out_bar) {
    uint32_t low;
    uint32_t high;

    if (!dev || !out_bar || bar_index >= 6U) return -1;

    memset(out_bar, 0, sizeof(*out_bar));
    out_bar->index = bar_index;
    low = pci_device_bar_value(dev, bar_index);
    out_bar->raw_low = low;
    if (low == 0U || low == 0xFFFFFFFFU) return -1;

    out_bar->present = 1;
    out_bar->is_io = (uint8_t)((low & 0x1U) != 0U);
    if (out_bar->is_io) {
        out_bar->base = (uint64_t)(low & ~0x3U);
        return 0;
    }

    out_bar->prefetchable = (uint8_t)((low & 0x8U) != 0U);
    out_bar->is_64 = (uint8_t)((low & 0x6U) == 0x4U);
    if (out_bar->is_64) {
        if (bar_index >= 5U) return -1;
        high = pci_device_bar_value(dev, (uint8_t)(bar_index + 1U));
        out_bar->raw_high = high;
        out_bar->base = ((uint64_t)high << 32) | (uint64_t)(low & ~0xFU);
    } else {
        out_bar->base = (uint64_t)(low & ~0xFU);
    }
    return 0;
}

uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = 0x80000000U |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) |
                       (uint32_t)(offset & 0xFCU);
    outl(PCI_CONFIG_ADDRESS, address);
    return inw((uint16_t)(PCI_CONFIG_DATA + (offset & 0x02U)));
}

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = 0x80000000U |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) |
                       (uint32_t)(offset & 0xFCU);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value) {
    uint32_t address = 0x80000000U |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) |
                       (uint32_t)(offset & 0xFCU);
    outl(PCI_CONFIG_ADDRESS, address);
    outw((uint16_t)(PCI_CONFIG_DATA + (offset & 0x02U)), value);
}

void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = 0x80000000U |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) |
                       (uint32_t)(offset & 0xFCU);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

int pci_decode_irq(const pci_device_info_t* dev, pci_irq_route_t* out_route) {
    if (!dev || !out_route) return -1;

    memset(out_route, 0, sizeof(*out_route));
    if (dev->interrupt_pin < 1U || dev->interrupt_pin > 4U) return -1;

    out_route->present = 1;
    out_route->shareable = 1;
    out_route->irq_line = dev->interrupt_line;
    out_route->irq_pin = dev->interrupt_pin;
    if (dev->interrupt_line == 0xFFU || dev->interrupt_line >= 16U || dev->interrupt_line == 2U) {
        return 0;
    }

    out_route->routed = 1;
    out_route->legacy_pic = 1;
    out_route->vector = (uint8_t)(0x20U + dev->interrupt_line);
    if (pci_legacy_irq_read_masked(dev->interrupt_line, &out_route->masked) != 0) {
        out_route->routed = 0;
        out_route->legacy_pic = 0;
    }
    return 0;
}

int pci_enable_irq(const pci_device_info_t* dev, pci_irq_route_t* out_route) {
    pci_irq_route_t route;

    if (pci_decode_irq(dev, &route) != 0 || !route.routed || !route.legacy_pic) return -1;
    if (pci_legacy_irq_set_masked(route.irq_line, 0) != 0) return -1;
    route.masked = 0;
    if (out_route) *out_route = route;
    return 0;
}

int pci_enumerate(pci_device_info_t* out_devices, int max_devices) {
    return pci_enumerate_internal(out_devices, max_devices, 0);
}

int pci_enumerate_storage(pci_device_info_t* out_devices, int max_devices) {
    return pci_enumerate_internal(out_devices, max_devices, 1);
}

int pci_device_count() {
    return pci_enumerate(0, 0);
}

int pci_storage_controller_count() {
    return pci_enumerate_storage(0, 0);
}

void pci_print_devices() {
    pci_device_info_t devices[64];
    int total = pci_enumerate(devices, 64);

    if (total == 0) {
        vga_println("PCI: no devices found.");
        return;
    }

    vga_print("PCI devices detected: ");
    {
        char buf[12];
        int off = 10;
        int value = total;
        buf[11] = '\0';
        if (value == 0) buf[off--] = '0';
        while (value > 0 && off >= 0) {
            buf[off--] = (char)('0' + (value % 10));
            value /= 10;
        }
        vga_print(&buf[off + 1]);
    }
    vga_println("");

    if (total > 64) {
        vga_println("Showing first 64 devices.");
        total = 64;
    }

    for (int i = 0; i < total; i++) {
        const pci_device_info_t* dev = &devices[i];
        pci_bar_info_t bar0_info;
        pci_irq_route_t irq_info;
        vga_print("  ");
        pci_print_hex8(dev->bus);
        vga_print(":");
        pci_print_hex8(dev->slot);
        vga_print(".");
        pci_print_hex8(dev->func);
        vga_print("  ");
        pci_print_hex16(dev->vendor_id);
        vga_print(":");
        pci_print_hex16(dev->device_id);
        vga_print("  ");
        vga_print(pci_class_name(dev->class_code, dev->subclass));
        vga_print("  class ");
        pci_print_hex8(dev->class_code);
        vga_print("/");
        pci_print_hex8(dev->subclass);
        vga_print("  if ");
        pci_print_hex8(dev->prog_if);
        if (pci_decode_bar(dev, 0, &bar0_info) == 0) {
            vga_print("  bar0 ");
            if (bar0_info.is_io) {
                vga_print("IO ");
                pci_print_hex32((uint32_t)bar0_info.base);
            } else {
                vga_print(bar0_info.is_64 ? "MM64 " : "MM32 ");
                pci_print_hex32((uint32_t)bar0_info.base);
            }
        }
        if (pci_decode_irq(dev, &irq_info) == 0) {
            pci_print_irq_short(&irq_info);
        }
        vga_println("");
    }
}

void pci_print_storage_devices() {
    pci_device_info_t devices[32];
    int total = pci_enumerate_storage(devices, 32);

    if (total == 0) {
        vga_println("Storage controllers detected: 0");
        return;
    }

    vga_print("Storage controllers detected: ");
    vga_print_int(total);
    vga_println("");
    if (total > 32) {
        vga_println("Showing first 32 storage controllers.");
        total = 32;
    }

    for (int i = 0; i < total; i++) {
        const pci_device_info_t* dev = &devices[i];
        pci_irq_route_t irq_info;

        vga_print("  ");
        pci_print_hex8(dev->bus);
        vga_print(":");
        pci_print_hex8(dev->slot);
        vga_print(".");
        pci_print_hex8(dev->func);
        vga_print("  ");
        pci_print_hex16(dev->vendor_id);
        vga_print(":");
        pci_print_hex16(dev->device_id);
        vga_print("  ");
        vga_println(pci_storage_controller_name(dev));

        vga_print("    class/sub/if ");
        pci_print_hex8(dev->class_code);
        vga_print("/");
        pci_print_hex8(dev->subclass);
        vga_print("/");
        pci_print_hex8(dev->prog_if);
        vga_println("");
        if (pci_decode_irq(dev, &irq_info) == 0) {
            pci_print_irq_detail_line(&irq_info);
        }

        for (int bar_idx = 0; bar_idx < 6; bar_idx++) {
            pci_bar_info_t bar_info;
            char prefix[10] = "    BAR0 ";
            prefix[7] = (char)('0' + bar_idx);
            if (pci_decode_bar(dev, (uint8_t)bar_idx, &bar_info) != 0) continue;
            pci_print_bar_line(prefix, bar_info.raw_low, bar_info.raw_high);
            if (bar_info.is_64) bar_idx++;
        }
    }
}
