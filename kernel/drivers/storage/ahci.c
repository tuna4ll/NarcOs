#include "ahci.h"
#include "paging.h"
#include "io.h"
#include "string.h"
#include "serial.h"

#define AHCI_CLASS_CODE         0x01U
#define AHCI_SUBCLASS_SATA      0x06U
#define AHCI_PROGIF_AHCI        0x01U

#define HBA_PORT_DET_PRESENT    0x3U
#define HBA_PORT_IPM_ACTIVE     0x1U
#define HBA_SIG_SATA            0x00000101U

#define HBA_GHC_AE              (1U << 31)
#define HBA_CAP2_BOH            0x1U
#define HBA_BOHC_BOS            (1U << 0)
#define HBA_BOHC_OOS            (1U << 1)
#define HBA_BOHC_BB             (1U << 4)

#define HBA_PxCMD_ST            0x0001U
#define HBA_PxCMD_FRE           0x0010U
#define HBA_PxCMD_FR            0x4000U
#define HBA_PxCMD_CR            0x8000U
#define HBA_PxIS_TFES           (1U << 30)

#define ATA_DEV_BUSY            0x80U
#define ATA_DEV_DRQ             0x08U

#define ATA_CMD_IDENTIFY        0xECU
#define ATA_CMD_READ_DMA_EXT    0x25U
#define ATA_CMD_WRITE_DMA_EXT   0x35U
#define ATA_CMD_FLUSH_CACHE_EXT 0xEAU

#define FIS_TYPE_REG_H2D        0x27U

#define AHCI_TIMEOUT            1000000U

typedef volatile struct {
    uint32_t clb;
    uint32_t clbu;
    uint32_t fb;
    uint32_t fbu;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t reserved0;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
    uint32_t sntf;
    uint32_t fbs;
    uint32_t reserved1[11];
    uint32_t vendor[4];
} hba_port_t;

typedef volatile struct {
    uint32_t cap;
    uint32_t ghc;
    uint32_t is;
    uint32_t pi;
    uint32_t vs;
    uint32_t ccc_ctl;
    uint32_t ccc_pts;
    uint32_t em_loc;
    uint32_t em_ctl;
    uint32_t cap2;
    uint32_t bohc;
    uint8_t reserved[0xA0 - 0x2C];
    uint8_t vendor[0x100 - 0xA0];
    hba_port_t ports[32];
} hba_mem_t;

typedef struct {
    uint8_t cfl;
    uint8_t flags;
    uint16_t prdtl;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t reserved[4];
} hba_cmd_header_t;

typedef struct {
    uint32_t dba;
    uint32_t dbau;
    uint32_t reserved0;
    uint32_t dbc;
} hba_prdt_entry_t;

typedef struct {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t reserved[48];
    hba_prdt_entry_t prdt_entry[1];
} hba_cmd_tbl_t;

typedef struct {
    uint8_t fis_type;
    uint8_t pmport_c;
    uint8_t command;
    uint8_t featurel;
    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;
    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t featureh;
    uint8_t countl;
    uint8_t counth;
    uint8_t icc;
    uint8_t control;
    uint8_t reserved1[4];
} fis_reg_h2d_t;

typedef struct {
    int active;
    int irq_enabled;
    int initialized;
    uint8_t port_index;
    uint64_t abar_phys;
    uint64_t sector_count;
    char model[41];
    pci_device_info_t controller;
    pci_irq_route_t irq_route;
    hba_mem_t* abar;
    hba_port_t* port;
    void* clb_page;
    void* fb_page;
    void* cmdtbl_page;
} ahci_state_t;

static ahci_state_t ahci_state;
static uint16_t ahci_identify_words[256];

static void ahci_log_port(const char* prefix, uint8_t value) {
    serial_write(prefix);
    serial_write_hex32((uint32_t)value);
    serial_write_char('\n');
}

static int ahci_wait_clear32(volatile uint32_t* reg, uint32_t mask) {
    for (uint32_t i = 0; i < AHCI_TIMEOUT; i++) {
        if ((*reg & mask) == 0U) return 0;
    }
    return -1;
}

static int ahci_port_is_sata(hba_port_t* port) {
    uint32_t ssts;
    uint8_t det;
    uint8_t ipm;

    if (!port) return 0;
    ssts = port->ssts;
    det = (uint8_t)(ssts & 0x0FU);
    ipm = (uint8_t)((ssts >> 8) & 0x0FU);
    if (det != HBA_PORT_DET_PRESENT || ipm != HBA_PORT_IPM_ACTIVE) return 0;
    return port->sig == HBA_SIG_SATA;
}

static int ahci_find_controller(pci_device_info_t* out_dev) {
    pci_device_info_t devices[32];
    int total = pci_enumerate_storage(devices, 32);

    if (total > 32) total = 32;
    for (int i = 0; i < total; i++) {
        const pci_device_info_t* dev = &devices[i];
        if (dev->class_code != AHCI_CLASS_CODE) continue;
        if (dev->subclass != AHCI_SUBCLASS_SATA) continue;
        if (dev->prog_if != AHCI_PROGIF_AHCI) continue;
        if (out_dev) *out_dev = *dev;
        return 0;
    }
    return -1;
}

static void ahci_bios_handoff(hba_mem_t* abar) {
    if (!abar) return;
    if ((abar->cap2 & HBA_CAP2_BOH) == 0U) return;

    abar->bohc |= HBA_BOHC_OOS;
    for (uint32_t i = 0; i < AHCI_TIMEOUT; i++) {
        if ((abar->bohc & (HBA_BOHC_BOS | HBA_BOHC_BB)) == 0U) return;
    }
}

static void ahci_stop_port(hba_port_t* port) {
    if (!port) return;
    port->cmd &= ~HBA_PxCMD_ST;
    port->cmd &= ~HBA_PxCMD_FRE;
    (void)ahci_wait_clear32(&port->cmd, HBA_PxCMD_FR | HBA_PxCMD_CR);
}

static int ahci_start_port(hba_port_t* port) {
    if (!port) return -1;
    if (ahci_wait_clear32(&port->cmd, HBA_PxCMD_CR) != 0) return -1;
    port->cmd |= HBA_PxCMD_FRE;
    port->cmd |= HBA_PxCMD_ST;
    return 0;
}

static int ahci_setup_port(hba_port_t* port) {
    hba_cmd_header_t* cmd_header;

    ahci_stop_port(port);

    ahci_state.clb_page = alloc_physical_pages(1);
    ahci_state.fb_page = alloc_physical_pages(1);
    ahci_state.cmdtbl_page = alloc_physical_pages(1);
    if (!ahci_state.clb_page || !ahci_state.fb_page || !ahci_state.cmdtbl_page) return -1;

    memset(ahci_state.clb_page, 0, 4096);
    memset(ahci_state.fb_page, 0, 4096);
    memset(ahci_state.cmdtbl_page, 0, 4096);

    port->clb = (uint32_t)((uintptr_t)ahci_state.clb_page & 0xFFFFFFFFU);
    port->clbu = (uint32_t)(((uint64_t)(uintptr_t)ahci_state.clb_page >> 32) & 0xFFFFFFFFU);
    port->fb = (uint32_t)((uintptr_t)ahci_state.fb_page & 0xFFFFFFFFU);
    port->fbu = (uint32_t)(((uint64_t)(uintptr_t)ahci_state.fb_page >> 32) & 0xFFFFFFFFU);
    port->is = 0xFFFFFFFFU;
    port->serr = 0xFFFFFFFFU;
    port->ie = 0;

    cmd_header = (hba_cmd_header_t*)ahci_state.clb_page;
    memset(cmd_header, 0, 4096);
    cmd_header[0].prdtl = 1;
    cmd_header[0].ctba = (uint32_t)((uintptr_t)ahci_state.cmdtbl_page & 0xFFFFFFFFU);
    cmd_header[0].ctbau = (uint32_t)(((uint64_t)(uintptr_t)ahci_state.cmdtbl_page >> 32) & 0xFFFFFFFFU);

    return ahci_start_port(port);
}

static int ahci_wait_port_ready(hba_port_t* port) {
    if (!port) return -1;
    for (uint32_t i = 0; i < AHCI_TIMEOUT; i++) {
        if ((port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) == 0U) return 0;
    }
    return -1;
}

static int ahci_exec_command(uint8_t command, uint64_t lba, uint16_t sector_count,
                             void* buffer, uint32_t byte_count, int write) {
    hba_cmd_header_t* header;
    hba_cmd_tbl_t* table;
    fis_reg_h2d_t* fis;
    hba_port_t* port = ahci_state.port;

    if (!ahci_state.active || !port) return -1;
    if ((port->ci & 0x1U) != 0U) return -1;
    if (ahci_wait_port_ready(port) != 0) return -1;

    header = (hba_cmd_header_t*)ahci_state.clb_page;
    table = (hba_cmd_tbl_t*)ahci_state.cmdtbl_page;
    memset(table, 0, 4096);

    header[0].cfl = (uint8_t)(sizeof(fis_reg_h2d_t) / sizeof(uint32_t));
    header[0].flags = write ? (1U << 6) : 0U;
    header[0].prdtl = byte_count != 0U ? 1U : 0U;
    header[0].prdbc = 0;

    if (byte_count != 0U) {
        uint64_t buffer_addr = (uint64_t)(uintptr_t)buffer;

        table->prdt_entry[0].dba = (uint32_t)(buffer_addr & 0xFFFFFFFFU);
        table->prdt_entry[0].dbau = (uint32_t)((buffer_addr >> 32) & 0xFFFFFFFFU);
        table->prdt_entry[0].reserved0 = 0;
        table->prdt_entry[0].dbc = ((byte_count - 1U) & 0x003FFFFFU) | (1U << 31);
    }

    fis = (fis_reg_h2d_t*)&table->cfis[0];
    memset(fis, 0, sizeof(*fis));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pmport_c = 1U << 7;
    fis->command = command;
    fis->device = 1U << 6;
    fis->lba0 = (uint8_t)(lba & 0xFFU);
    fis->lba1 = (uint8_t)((lba >> 8) & 0xFFU);
    fis->lba2 = (uint8_t)((lba >> 16) & 0xFFU);
    fis->lba3 = (uint8_t)((lba >> 24) & 0xFFU);
    fis->lba4 = (uint8_t)((lba >> 32) & 0xFFU);
    fis->lba5 = (uint8_t)((lba >> 40) & 0xFFU);
    fis->countl = (uint8_t)(sector_count & 0xFFU);
    fis->counth = (uint8_t)((sector_count >> 8) & 0xFFU);

    port->is = 0xFFFFFFFFU;
    port->ci = 0x1U;
    for (uint32_t i = 0; i < AHCI_TIMEOUT; i++) {
        if ((port->ci & 0x1U) == 0U) {
            if ((port->is & HBA_PxIS_TFES) != 0U) return -1;
            return 0;
        }
        if ((port->is & HBA_PxIS_TFES) != 0U) return -1;
    }
    return -1;
}

static void ahci_parse_identify(void) {
    int out = 0;

    memset(ahci_state.model, 0, sizeof(ahci_state.model));
    for (int i = 27; i <= 46 && out < 40; i++) {
        ahci_state.model[out++] = (char)((ahci_identify_words[i] >> 8) & 0xFF);
        if (out < 40) ahci_state.model[out++] = (char)(ahci_identify_words[i] & 0xFF);
    }
    while (out > 0 && ahci_state.model[out - 1] == ' ') {
        ahci_state.model[out - 1] = '\0';
        out--;
    }
    ahci_state.model[40] = '\0';

    ahci_state.sector_count =
        ((uint64_t)ahci_identify_words[103] << 48) |
        ((uint64_t)ahci_identify_words[102] << 32) |
        ((uint64_t)ahci_identify_words[101] << 16) |
        (uint64_t)ahci_identify_words[100];
    if (ahci_state.sector_count == 0) {
        ahci_state.sector_count =
            ((uint64_t)ahci_identify_words[61] << 16) |
            (uint64_t)ahci_identify_words[60];
    }
}

int ahci_init(void) {
    pci_bar_info_t abar_bar;
    uint16_t pci_command;
    uint32_t ports;

    memset(&ahci_state, 0, sizeof(ahci_state));
    ahci_state.port_index = 0xFFU;

    if (ahci_find_controller(&ahci_state.controller) != 0) return -1;
    if (pci_decode_bar(&ahci_state.controller, 5, &abar_bar) != 0) return -1;
    if (abar_bar.is_io) return -1;

    pci_command = pci_read16(ahci_state.controller.bus, ahci_state.controller.slot,
                             ahci_state.controller.func, 0x04);
    pci_command |= 0x0006U;
    pci_command &= (uint16_t)~0x0400U;
    pci_write16(ahci_state.controller.bus, ahci_state.controller.slot,
                ahci_state.controller.func, 0x04, pci_command);

    if (pci_enable_irq(&ahci_state.controller, &ahci_state.irq_route) == 0) {
        ahci_state.irq_enabled = 1;
    } else {
        (void)pci_decode_irq(&ahci_state.controller, &ahci_state.irq_route);
    }

    ahci_state.abar_phys = abar_bar.base;
    ahci_state.abar = (hba_mem_t*)paging_map_physical(ahci_state.abar_phys, 0x2000U,
                                                      PAGING_FLAG_WRITE | PAGING_FLAG_CACHE_DISABLE);
    if (!ahci_state.abar) return -1;

    ahci_state.abar->ghc |= HBA_GHC_AE;
    ahci_bios_handoff(ahci_state.abar);
    ports = ahci_state.abar->pi;
    for (uint8_t port = 0; port < 32; port++) {
        if ((ports & (1U << port)) == 0U) continue;
        if (!ahci_port_is_sata(&ahci_state.abar->ports[port])) continue;
        ahci_state.port_index = port;
        ahci_state.port = &ahci_state.abar->ports[port];
        break;
    }
    if (!ahci_state.port || ahci_state.port_index == 0xFFU) return -1;
    if (ahci_setup_port(ahci_state.port) != 0) return -1;
    ahci_state.active = 1;
    if (ahci_exec_command(ATA_CMD_IDENTIFY, 0, 1, ahci_identify_words, 512U, 0) != 0) {
        ahci_state.active = 0;
        return -1;
    }
    ahci_parse_identify();
    ahci_state.initialized = 1;

    serial_write_line("[storage] ahci controller online");
    serial_write("[storage] ahci abar=");
    serial_write_hex64(ahci_state.abar_phys);
    serial_write_char('\n');
    ahci_log_port("[storage] ahci port=", ahci_state.port_index);
    return 0;
}

int ahci_is_active(void) {
    return ahci_state.active;
}

int ahci_read_sector(uint32_t lba, uint8_t* buffer) {
    if (!buffer || !ahci_state.active) return -1;
    return ahci_exec_command(ATA_CMD_READ_DMA_EXT, (uint64_t)lba, 1, buffer, 512U, 0);
}

int ahci_write_sector(uint32_t lba, const uint8_t* buffer) {
    if (!buffer || !ahci_state.active) return -1;
    if (ahci_exec_command(ATA_CMD_WRITE_DMA_EXT, (uint64_t)lba, 1, (void*)buffer, 512U, 1) != 0) return -1;
    return ahci_exec_command(ATA_CMD_FLUSH_CACHE_EXT, 0, 0, 0, 0, 0);
}

int ahci_get_info(ahci_info_t* out_info) {
    if (!out_info || !ahci_state.initialized) return -1;

    memset(out_info, 0, sizeof(*out_info));
    out_info->active = ahci_state.active;
    out_info->irq_enabled = ahci_state.irq_enabled;
    out_info->port_index = ahci_state.port_index;
    out_info->abar_phys = ahci_state.abar_phys;
    out_info->sector_count = ahci_state.sector_count;
    memcpy(out_info->model, ahci_state.model, sizeof(out_info->model));
    out_info->controller = ahci_state.controller;
    out_info->irq_route = ahci_state.irq_route;
    return 0;
}
