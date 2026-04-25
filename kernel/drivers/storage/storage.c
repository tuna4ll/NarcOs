#include "storage.h"
#include "ahci.h"
#include "ata.h"
#include "fs.h"
#include "serial.h"
#include "string.h"

extern void vga_print(const char* str);
extern void vga_println(const char* str);
extern void vga_print_int(int num);
extern void vga_print_int_hex(uint32_t n, char* buf);

static storage_backend_t storage_backend = STORAGE_BACKEND_NONE;
static storage_partition_scheme_t storage_active_scheme = STORAGE_PARTITION_SCHEME_NONE;
static uint32_t storage_volume_base = 0;
static storage_partition_info_t storage_partitions[8];
static int storage_partition_total = 0;

#define STORAGE_DIR_SECTOR        2048U
#define STORAGE_DIR_SECTOR_COUNT  8U
#define STORAGE_MBR_SIGNATURE     0xAA55U
#define STORAGE_GPT_ENTRY_SIZE    128U
#define STORAGE_GPT_MAX_ENTRIES   128U

typedef struct {
    uint8_t boot_indicator;
    uint8_t start_chs[3];
    uint8_t type;
    uint8_t end_chs[3];
    uint32_t start_lba;
    uint32_t sector_count;
} __attribute__((packed)) storage_mbr_partition_t;

typedef struct {
    char signature[8];
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t disk_guid[16];
    uint64_t entries_lba;
    uint32_t entry_count;
    uint32_t entry_size;
    uint32_t entries_crc32;
} __attribute__((packed)) storage_gpt_header_t;

typedef struct {
    uint8_t type_guid[16];
    uint8_t unique_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
    uint16_t name[36];
} __attribute__((packed)) storage_gpt_entry_t;

static const uint8_t storage_gpt_guid_efi_system[16] = {
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};

static const uint8_t storage_gpt_guid_basic_data[16] = {
    0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
    0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
};

static const uint8_t storage_gpt_guid_linux_fs[16] = {
    0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
    0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4
};

static const uint8_t storage_gpt_guid_linux_swap[16] = {
    0x6D, 0xFD, 0x57, 0x06, 0xAB, 0xA4, 0xC4, 0x43,
    0x84, 0xE5, 0x09, 0x33, 0xC8, 0x4B, 0x4F, 0x4F
};

static const uint8_t storage_gpt_signature[8] = {
    'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'
};

static int storage_bytes_equal(const uint8_t* a, const uint8_t* b, uint32_t len) {
    if (!a || !b) return 0;
    for (uint32_t i = 0; i < len; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static uint32_t storage_effective_volume_base(void) {
#if UINTPTR_MAX > 0xFFFFFFFFU
    return 0U;
#else
    return storage_volume_base;
#endif
}

static void storage_print_hex16(uint16_t value) {
    char buf[11];
    vga_print_int_hex((uint32_t)value, buf);
    vga_print(buf + 6);
}

static void storage_print_hex8(uint8_t value) {
    char buf[11];
    vga_print_int_hex((uint32_t)value, buf);
    vga_print(buf + 8);
}

static const char* storage_partition_scheme_name(storage_partition_scheme_t scheme) {
    switch (scheme) {
        case STORAGE_PARTITION_SCHEME_MBR: return "MBR";
        case STORAGE_PARTITION_SCHEME_GPT: return "GPT";
        default: return "RAW";
    }
}

static const char* storage_partition_type_name(const storage_partition_info_t* part) {
    if (!part) return "Unknown";
    if (part->scheme == STORAGE_PARTITION_SCHEME_GPT) {
        if (storage_bytes_equal(part->type_guid, storage_gpt_guid_efi_system, 16)) return "EFI System";
        if (storage_bytes_equal(part->type_guid, storage_gpt_guid_basic_data, 16)) return "Basic Data";
        if (storage_bytes_equal(part->type_guid, storage_gpt_guid_linux_fs, 16)) return "Linux Filesystem";
        if (storage_bytes_equal(part->type_guid, storage_gpt_guid_linux_swap, 16)) return "Linux Swap";
        return "GPT Partition";
    }

    switch (part->type) {
        case 0x01: return "FAT12";
        case 0x04: return "FAT16";
        case 0x05: return "Extended";
        case 0x06: return "FAT16B";
        case 0x07: return "NTFS/ExFAT";
        case 0x0B: return "FAT32";
        case 0x0C: return "FAT32 LBA";
        case 0x0F: return "Extended LBA";
        case 0x82: return "Linux Swap";
        case 0x83: return "Linux";
        case 0xA5: return "BSD";
        case 0xAF: return "HFS+";
        case 0xEE: return "GPT Protective";
        default: return "Unknown";
    }
}

static int storage_backend_read_sector(storage_backend_t backend, uint32_t lba, uint8_t* buffer) {
    if (!buffer) return -1;

    if (backend == STORAGE_BACKEND_AHCI) {
        return ahci_read_sector(lba, buffer);
    }
    if (backend == STORAGE_BACKEND_ATA_PIO) {
        ata_read_sector(lba, buffer);
        return 0;
    }
    return -1;
}

static int storage_backend_write_sector(storage_backend_t backend, uint32_t lba, const uint8_t* buffer) {
    if (!buffer) return -1;

    if (backend == STORAGE_BACKEND_AHCI) {
        return ahci_write_sector(lba, buffer);
    }
    if (backend == STORAGE_BACKEND_ATA_PIO) {
        ata_write_sector(lba, (uint8_t*)buffer);
        return 0;
    }
    return -1;
}

static void storage_clear_partitions(void) {
    memset(storage_partitions, 0, sizeof(storage_partitions));
    storage_partition_total = 0;
}

static int storage_find_child(const disk_fs_node_t* nodes, int parent_idx, const char* name, uint32_t type) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (nodes[i].flags == 0) continue;
        if (nodes[i].parent_index != parent_idx) continue;
        if (type != 0 && nodes[i].flags != type) continue;
        if (strcmp(nodes[i].name, name) == 0) return i;
    }
    return -1;
}

static int storage_probe_narcos_volume(storage_backend_t backend, uint32_t base_lba) {
    disk_fs_node_t nodes[MAX_FILES];

    memset(nodes, 0, sizeof(nodes));
    for (uint32_t i = 0; i < STORAGE_DIR_SECTOR_COUNT; i++) {
        if (storage_backend_read_sector(backend, base_lba + STORAGE_DIR_SECTOR + i,
                                        (uint8_t*)nodes + (i * 512U)) != 0) {
            return 0;
        }
    }

    {
        int home = storage_find_child(nodes, -1, "home", FS_NODE_DIR);
        int user = home >= 0 ? storage_find_child(nodes, home, "user", FS_NODE_DIR) : -1;
        int desktop = user >= 0 ? storage_find_child(nodes, user, "Desktop", FS_NODE_DIR) : -1;
        return home >= 0 && user >= 0 && desktop >= 0;
    }
}

static int storage_append_partition(storage_partition_scheme_t scheme, uint8_t bootable, uint8_t type,
                                    const uint8_t* type_guid, uint32_t start_lba, uint32_t sector_count) {
    storage_partition_info_t* part;

    if (storage_partition_total >= (int)(sizeof(storage_partitions) / sizeof(storage_partitions[0]))) return -1;
    part = &storage_partitions[storage_partition_total++];
    memset(part, 0, sizeof(*part));
    part->scheme = (uint8_t)scheme;
    part->valid = 1;
    part->bootable = bootable;
    part->type = type;
    part->start_lba = start_lba;
    part->sector_count = sector_count;
    if (type_guid) memcpy(part->type_guid, type_guid, 16);
    return 0;
}

static int storage_parse_mbr(storage_backend_t backend) {
    uint8_t sector[512];
    const storage_mbr_partition_t* entries;
    int has_partitions = 0;

    storage_clear_partitions();
    if (storage_backend_read_sector(backend, 0, sector) != 0) return 0;
    if ((uint16_t)(sector[510] | ((uint16_t)sector[511] << 8)) != STORAGE_MBR_SIGNATURE) return 0;

    entries = (const storage_mbr_partition_t*)(sector + 446);
    for (int i = 0; i < 4; i++) {
        const storage_mbr_partition_t* entry = &entries[i];

        if (entry->boot_indicator != 0x00U && entry->boot_indicator != 0x80U) return 0;
        if (entry->type == 0x00U) {
            if (entry->start_lba != 0U || entry->sector_count != 0U) return 0;
            continue;
        }
        if (entry->start_lba == 0U || entry->sector_count == 0U) return 0;
        if (storage_append_partition(STORAGE_PARTITION_SCHEME_MBR,
                                     (uint8_t)(entry->boot_indicator == 0x80U),
                                     entry->type, 0, entry->start_lba, entry->sector_count) != 0) {
            break;
        }
        has_partitions = 1;
    }

    return has_partitions;
}

static int storage_gpt_type_is_unused(const uint8_t* guid) {
    uint8_t zero[16];
    memset(zero, 0, sizeof(zero));
    return storage_bytes_equal(guid, zero, sizeof(zero));
}

static int storage_parse_gpt(storage_backend_t backend) {
    uint8_t sector[512];
    storage_gpt_header_t header;
    uint8_t entry_sector[512];
    uint32_t entries_per_sector;
    uint32_t total_entries;
    uint32_t entry_size;
    uint64_t entries_lba;
    int has_partitions = 0;

    storage_clear_partitions();
    if (storage_backend_read_sector(backend, 1, sector) != 0) return 0;
    memcpy(&header, sector, sizeof(header));
    if (!storage_bytes_equal((const uint8_t*)header.signature, storage_gpt_signature, 8)) return 0;
    if (header.header_size < 92U || header.header_size > 512U) return 0;
    if (header.entry_size < STORAGE_GPT_ENTRY_SIZE || header.entry_size > 512U) return 0;
    if ((header.entry_size % 8U) != 0U) return 0;
    if (header.entry_count == 0U || header.entry_count > STORAGE_GPT_MAX_ENTRIES) return 0;
    if (header.entries_lba == 0U || header.entries_lba > 0xFFFFFFFFULL) return 0;

    entry_size = header.entry_size;
    total_entries = header.entry_count;
    entries_lba = header.entries_lba;
    entries_per_sector = 512U / entry_size;
    if (entries_per_sector == 0U) return 0;

    for (uint32_t i = 0; i < total_entries; i++) {
        uint32_t sector_index = i / entries_per_sector;
        uint32_t entry_index = i % entries_per_sector;
        const storage_gpt_entry_t* entry;
        const uint8_t* entry_bytes;
        uint64_t first_lba;
        uint64_t last_lba;

        if (storage_backend_read_sector(backend, (uint32_t)(entries_lba + sector_index), entry_sector) != 0) return 0;
        entry_bytes = entry_sector + entry_index * entry_size;
        entry = (const storage_gpt_entry_t*)entry_bytes;
        if (storage_gpt_type_is_unused(entry->type_guid)) continue;

        first_lba = entry->first_lba;
        last_lba = entry->last_lba;
        if (first_lba == 0U || last_lba < first_lba || last_lba > 0xFFFFFFFFULL) continue;
        if ((last_lba - first_lba + 1ULL) > 0xFFFFFFFFULL) continue;
        if (storage_append_partition(STORAGE_PARTITION_SCHEME_GPT, 0, 0, entry->type_guid,
                                     (uint32_t)first_lba, (uint32_t)(last_lba - first_lba + 1ULL)) != 0) {
            break;
        }
        has_partitions = 1;
    }

    return has_partitions;
}

static int storage_scan_partitions(storage_backend_t backend) {
    if (storage_parse_gpt(backend)) return STORAGE_PARTITION_SCHEME_GPT;
    if (storage_parse_mbr(backend)) return STORAGE_PARTITION_SCHEME_MBR;
    storage_clear_partitions();
    return STORAGE_PARTITION_SCHEME_NONE;
}

static int storage_select_volume_for_backend(storage_backend_t backend) {
    storage_active_scheme = STORAGE_PARTITION_SCHEME_NONE;
    storage_volume_base = 0;
    (void)storage_scan_partitions(backend);

    for (int i = 0; i < storage_partition_total; i++) {
        if (storage_probe_narcos_volume(backend, storage_partitions[i].start_lba)) {
            storage_partitions[i].active_volume = 1;
            storage_volume_base = storage_partitions[i].start_lba;
            storage_active_scheme = (storage_partition_scheme_t)storage_partitions[i].scheme;
            return 1;
        }
    }

    return storage_probe_narcos_volume(backend, 0);
}

static void storage_log_backend_selection(void) {
    serial_write("[storage] backend=");
    serial_write(storage_backend == STORAGE_BACKEND_AHCI ? "ahci" : "ata-pio");
    serial_write(" volume_base=");
    serial_write_hex32(storage_effective_volume_base());
    serial_write(" scheme=");
    serial_write(storage_partition_scheme_name(storage_active_scheme));
    serial_write_char('\n');
}

const char* storage_backend_name(void) {
    switch (storage_backend) {
        case STORAGE_BACKEND_AHCI: return "AHCI SATA";
        case STORAGE_BACKEND_ATA_PIO: return "ATA PIO";
        default: return "none";
    }
}

storage_backend_t storage_get_backend(void) {
    return storage_backend;
}

uint32_t storage_volume_base_lba(void) {
    return storage_effective_volume_base();
}

storage_partition_scheme_t storage_volume_scheme(void) {
    return storage_active_scheme;
}

int storage_partition_count(void) {
    return storage_partition_total;
}

int storage_get_partition_info(int index, storage_partition_info_t* out_info) {
    if (!out_info || index < 0 || index >= storage_partition_total) return -1;
    *out_info = storage_partitions[index];
    return 0;
}

void storage_init(void) {
    serial_write_line("[storage] init");

#if UINTPTR_MAX > 0xFFFFFFFFU
    /* The current x86_64 boot path always uses the raw disk image layout.
       Skip partition probing until the long-mode storage path is stabilized. */
    storage_backend = STORAGE_BACKEND_ATA_PIO;
    storage_clear_partitions();
    storage_active_scheme = STORAGE_PARTITION_SCHEME_NONE;
    storage_volume_base = 0;
    serial_write_line("[storage] x86_64 raw ATA mode");
    storage_log_backend_selection();
    return;
#endif

    storage_backend = STORAGE_BACKEND_ATA_PIO;
    (void)storage_select_volume_for_backend(STORAGE_BACKEND_ATA_PIO);

    if (ahci_init() == 0) {
        if (storage_select_volume_for_backend(STORAGE_BACKEND_AHCI)) {
            storage_backend = STORAGE_BACKEND_AHCI;
            storage_log_backend_selection();
            return;
        }

        serial_write_line("[storage] ahci disk skipped (unknown filesystem layout)");
    }

    storage_backend = STORAGE_BACKEND_ATA_PIO;
    (void)storage_select_volume_for_backend(STORAGE_BACKEND_ATA_PIO);
    storage_log_backend_selection();
}

int storage_read_sector(uint32_t lba, uint8_t* buffer) {
    if (!buffer) return -1;
    return storage_backend_read_sector(storage_backend, storage_effective_volume_base() + lba, buffer);
}

int storage_write_sector(uint32_t lba, const uint8_t* buffer) {
    if (!buffer) return -1;
    return storage_backend_write_sector(storage_backend, storage_effective_volume_base() + lba, buffer);
}

void storage_print_status(void) {
    ahci_info_t info;

    vga_print("Active Storage : ");
    vga_println(storage_backend_name());
    vga_print("  Volume Base  : ");
    vga_print_int((int)storage_effective_volume_base());
    vga_print(" (");
    vga_print(storage_partition_scheme_name(storage_active_scheme));
    vga_println(storage_effective_volume_base() != 0U ? " partition)" : " disk)");
    vga_print("  Partitions   : ");
    vga_print_int(storage_partition_total);
    vga_println("");

    for (int i = 0; i < storage_partition_total; i++) {
        const storage_partition_info_t* part = &storage_partitions[i];
        vga_print("    ");
        vga_print(part->scheme == STORAGE_PARTITION_SCHEME_GPT ? "g" : "p");
        vga_print_int(i + 1);
        vga_print("  ");
        vga_print(storage_partition_type_name(part));
        if (part->scheme == STORAGE_PARTITION_SCHEME_MBR) {
            vga_print("  type ");
            storage_print_hex8(part->type);
        }
        if (part->bootable) vga_print("  boot");
        if (part->active_volume) vga_print("  active");
        vga_println("");
        vga_print("      start ");
        vga_print_int((int)part->start_lba);
        vga_print("  sectors ");
        vga_print_int((int)part->sector_count);
        vga_println("");
    }

    if (storage_backend != STORAGE_BACKEND_AHCI) {
        vga_println("  Path         : Legacy ATA primary bus");
        return;
    }
    if (ahci_get_info(&info) != 0 || !info.active) {
        vga_println("  Status       : AHCI init failed");
        return;
    }

    vga_print("  Controller   : ");
    storage_print_hex16(info.controller.vendor_id);
    vga_print(":");
    storage_print_hex16(info.controller.device_id);
    vga_println("");
    vga_print("  Port         : ");
    vga_print_int(info.port_index);
    vga_println("");
    vga_print("  Model        : ");
    vga_println(info.model[0] != '\0' ? info.model : "(unknown)");
    vga_print("  Sectors      : ");
    if (info.sector_count > 0xFFFFFFFFULL) {
        vga_print_int((int)(info.sector_count >> 32));
        vga_print(" x2^32 + ");
    }
    vga_print_int((int)(info.sector_count & 0xFFFFFFFFU));
    vga_println("");
    vga_print("  IRQ          : ");
    vga_print(pci_irq_pin_name(info.irq_route.irq_pin));
    if (info.irq_route.routed) {
        vga_print(" -> ");
        vga_print_int(info.irq_route.irq_line);
        vga_println(info.irq_enabled ? " (legacy PIC enabled)" : " (legacy PIC masked)");
    } else {
        vga_println(" -> unrouted");
    }
}
