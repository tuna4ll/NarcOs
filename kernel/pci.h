#ifndef PCI_H
#define PCI_H

#include <stdint.h>

typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t header_type;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t interrupt_line;
    uint8_t interrupt_pin;
    uint32_t bar0;
    uint32_t bar1;
    uint32_t bar2;
    uint32_t bar3;
    uint32_t bar4;
    uint32_t bar5;
} pci_device_info_t;

typedef struct {
    uint8_t index;
    uint8_t present;
    uint8_t is_io;
    uint8_t is_64;
    uint8_t prefetchable;
    uint32_t raw_low;
    uint32_t raw_high;
    uint64_t base;
} pci_bar_info_t;

typedef struct {
    uint8_t present;
    uint8_t routed;
    uint8_t legacy_pic;
    uint8_t shareable;
    uint8_t irq_line;
    uint8_t irq_pin;
    uint8_t vector;
    uint8_t masked;
} pci_irq_route_t;

uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value);
void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
int pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_info_t* out_device);
int pci_decode_bar(const pci_device_info_t* dev, uint8_t bar_index, pci_bar_info_t* out_bar);
int pci_decode_irq(const pci_device_info_t* dev, pci_irq_route_t* out_route);
int pci_enable_irq(const pci_device_info_t* dev, pci_irq_route_t* out_route);
int pci_enumerate(pci_device_info_t* out_devices, int max_devices);
int pci_enumerate_storage(pci_device_info_t* out_devices, int max_devices);
int pci_device_count();
int pci_storage_controller_count();
const char* pci_storage_controller_name(const pci_device_info_t* dev);
const char* pci_irq_pin_name(uint8_t pin);
void pci_print_devices();
void pci_print_storage_devices();

#endif
