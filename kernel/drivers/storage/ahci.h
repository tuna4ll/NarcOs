#ifndef AHCI_H
#define AHCI_H

#include <stdint.h>
#include "pci.h"

typedef struct {
    int active;
    int irq_enabled;
    uint8_t port_index;
    uint64_t abar_phys;
    uint64_t sector_count;
    char model[41];
    pci_device_info_t controller;
    pci_irq_route_t irq_route;
} ahci_info_t;

int ahci_init(void);
int ahci_is_active(void);
int ahci_read_sector(uint32_t lba, uint8_t* buffer);
int ahci_write_sector(uint32_t lba, const uint8_t* buffer);
int ahci_get_info(ahci_info_t* out_info);

#endif
