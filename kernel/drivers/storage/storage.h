#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>

typedef enum {
    STORAGE_BACKEND_NONE = 0,
    STORAGE_BACKEND_ATA_PIO,
    STORAGE_BACKEND_AHCI
} storage_backend_t;

typedef enum {
    STORAGE_PARTITION_SCHEME_NONE = 0,
    STORAGE_PARTITION_SCHEME_MBR,
    STORAGE_PARTITION_SCHEME_GPT
} storage_partition_scheme_t;

typedef struct {
    uint8_t scheme;
    uint8_t valid;
    uint8_t bootable;
    uint8_t type;
    uint8_t active_volume;
    uint32_t start_lba;
    uint32_t sector_count;
    uint8_t type_guid[16];
} storage_partition_info_t;

void storage_init(void);
int storage_read_sector(uint32_t lba, uint8_t* buffer);
int storage_write_sector(uint32_t lba, const uint8_t* buffer);
storage_backend_t storage_get_backend(void);
const char* storage_backend_name(void);
uint32_t storage_volume_base_lba(void);
storage_partition_scheme_t storage_volume_scheme(void);
int storage_partition_count(void);
int storage_get_partition_info(int index, storage_partition_info_t* out_info);
void storage_print_status(void);

#endif
