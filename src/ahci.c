#include "ahci.h"
#include "pci.h"
#include "paging.h"
#include <stdint.h>
#include <stddef.h>

static void ahci_memset(void *dst, uint8_t val, uint64_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (uint64_t i = 0; i < n; i++) d[i] = val;
}

typedef struct __attribute__((packed)) {
    volatile uint32_t cap;
    volatile uint32_t ghc;
    volatile uint32_t is;
    volatile uint32_t pi;
    volatile uint32_t vs;
    volatile uint32_t ccc_ctl;
    volatile uint32_t ccc_pts;
    volatile uint32_t em_loc;
    volatile uint32_t em_ctl;
    volatile uint32_t cap2;
    volatile uint32_t bohc;
    uint8_t reserved[0xA0 - 0x2C];
    uint8_t vendor[0x100 - 0xA0];
} hba_mem_t;

typedef struct __attribute__((packed)) {
    volatile uint32_t clb;
    volatile uint32_t clbu;
    volatile uint32_t fb;
    volatile uint32_t fbu;
    volatile uint32_t is;
    volatile uint32_t ie;
    volatile uint32_t cmd;
    volatile uint32_t reserved0;
    volatile uint32_t tfd;
    volatile uint32_t sig;
    volatile uint32_t ssts;
    volatile uint32_t sctl;
    volatile uint32_t serr;
    volatile uint32_t sact;
    volatile uint32_t ci;
    volatile uint32_t sntf;
    volatile uint32_t fbs;
    volatile uint32_t reserved1[11];
    volatile uint32_t vendor[4];
} hba_port_t;

typedef struct __attribute__((packed)) {
    uint8_t cfl : 5;
    uint8_t a : 1;
    uint8_t w : 1;
    uint8_t p : 1;
    uint8_t r : 1;
    uint8_t b : 1;
    uint8_t c : 1;
    uint8_t rsv0: 1;
    uint8_t pmp : 4;
    uint16_t prdtl;
    volatile uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t reserved1[4];
} hba_cmd_header_t;

typedef struct __attribute__((packed)) {
    uint32_t dba;
    uint32_t dbau;
    uint32_t reserved0;
    uint32_t dbc : 22;
    uint32_t rsv1: 9;
    uint32_t i : 1;
} hba_prdt_entry_t;

#define AHCI_MAX_PRDT_ENTRIES 8

typedef struct __attribute__((packed)) {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t reserved[48];
    hba_prdt_entry_t prdt_entry[AHCI_MAX_PRDT_ENTRIES];
} hba_cmd_tbl_t;

typedef struct __attribute__((packed)) {
    uint8_t fis_type;
    uint8_t pmport : 4;
    uint8_t rsv0 : 3;
    uint8_t c : 1;
    uint8_t command;
    uint8_t featurel;
    uint8_t lba0, lba1, lba2;
    uint8_t device;
    uint8_t lba3, lba4, lba5;
    uint8_t featureh;
    uint8_t countl, counth;
    uint8_t icc;
    uint8_t control;
    uint8_t reserved1[4];
} fis_reg_h2d_t;

#define HBA_GHC_AE (1u << 31)

#define HBA_PxCMD_ST (1u << 0)
#define HBA_PxCMD_FRE (1u << 4)
#define HBA_PxCMD_FR (1u << 14)
#define HBA_PxCMD_CR (1u << 15)

#define HBA_PxIS_TFES (1u << 30)

#define HBA_PORT_DET_PRESENT 0x3
#define HBA_PORT_IPM_ACTIVE 0x1

#define SATA_SIG_ATA 0x00000101u

#define ATA_CMD_READ_DMA_EX 0x25
#define ATA_CMD_WRITE_DMA_EX 0x35
#define ATA_CMD_IDENTIFY 0xEC

#define ATA_DEV_BUSY 0x80
#define ATA_DEV_DRQ 0x08

static hba_cmd_header_t g_cmd_list[32] __attribute__((aligned(1024)));
static uint8_t g_fis_base[256] __attribute__((aligned(256)));
static hba_cmd_tbl_t g_cmd_table __attribute__((aligned(128)));
static uint8_t g_identify_buffer[512] __attribute__((aligned(2)));


static hba_mem_t *g_hba = NULL;
static hba_port_t *g_port = NULL;
static uint64_t g_total_sectors = 0;

typedef struct {
    uint32_t port_index;
    hba_port_t *port;
    uint64_t total_sectors;
} ahci_disk_entry_t;

static ahci_disk_entry_t g_disks[AHCI_MAX_DISKS];
static uint32_t g_disk_count = 0;

static hba_port_t *hba_port_ptr(hba_mem_t *hba, uint32_t index) {
    uint8_t *base = (uint8_t *)hba;
    return (hba_port_t *)(base + 0x100 + index * 0x80);
}

static uint32_t ahci_find_all_sata_ports(hba_mem_t *hba, uint32_t *out_ports, uint32_t max_out) {
    uint32_t pi = hba->pi;
    uint32_t found = 0;
    for (uint32_t i = 0; i < 32; i++) {
        if (!(pi & (1u << i))) continue;

        hba_port_t *port = hba_port_ptr(hba, i);
        uint8_t det = (uint8_t)(port->ssts & 0x0F);
        uint8_t ipm = (uint8_t)((port->ssts >> 8) & 0x0F);

        if (det != HBA_PORT_DET_PRESENT) continue;
        if (ipm != HBA_PORT_IPM_ACTIVE) continue;
        if (port->sig != SATA_SIG_ATA) continue;

        if (found < max_out) out_ports[found] = i;
        found++;
    }
    return found;
}

static ahci_result_t ahci_stop_port(hba_port_t *port) {
    port->cmd &= ~HBA_PxCMD_ST;
    port->cmd &= ~HBA_PxCMD_FRE;

    uint32_t spin = 0;
    while (spin < 1000000u) {
        if (!(port->cmd & HBA_PxCMD_FR) && !(port->cmd & HBA_PxCMD_CR)) return AHCI_OK;
        spin++;
    }
    return AHCI_ERR_TIMEOUT;
}

static ahci_result_t ahci_start_port(hba_port_t *port) {
    uint32_t spin = 0;
    while (port->cmd & HBA_PxCMD_CR) {
        spin++;
        if (spin >= 1000000u) return AHCI_ERR_TIMEOUT;
    }
    port->cmd |= HBA_PxCMD_FRE;
    port->cmd |= HBA_PxCMD_ST;
    return AHCI_OK;
}

static ahci_result_t ahci_port_rebase(hba_port_t *port) {
    ahci_result_t res = ahci_stop_port(port);
    if (res != AHCI_OK) return res;

    ahci_memset(g_cmd_list, 0, sizeof(g_cmd_list));
    ahci_memset(g_fis_base, 0, sizeof(g_fis_base));
    ahci_memset(&g_cmd_table, 0, sizeof(g_cmd_table));

    uint64_t cmd_list_addr = (uint64_t)(uintptr_t)g_cmd_list;
    port->clb  = (uint32_t)(cmd_list_addr & 0xFFFFFFFFu);
    port->clbu = (uint32_t)(cmd_list_addr >> 32);

    uint64_t fis_addr = (uint64_t)(uintptr_t)g_fis_base;
    port->fb  = (uint32_t)(fis_addr & 0xFFFFFFFFu);
    port->fbu = (uint32_t)(fis_addr >> 32);

    uint64_t cmdtbl_addr = (uint64_t)(uintptr_t)&g_cmd_table;
    g_cmd_list[0].ctba = (uint32_t)(cmdtbl_addr & 0xFFFFFFFFu);
    g_cmd_list[0].ctbau = (uint32_t)(cmdtbl_addr >> 32);
    g_cmd_list[0].prdtl = 1;

    port->serr = 0xFFFFFFFFu;
    port->is = 0xFFFFFFFFu;
    port->ie = 0;

    return ahci_start_port(port);
}

static ahci_result_t ahci_issue_command(uint64_t lba, uint16_t sector_count, void *buffer, uint8_t ata_command, bool is_write) {
    if (g_port == NULL) return AHCI_ERR_NO_DRIVE;
    if (sector_count == 0) return AHCI_ERR_INVALID;

    g_port->is = 0xFFFFFFFFu;

    hba_cmd_header_t *hdr = &g_cmd_list[0];
    hdr->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
    hdr->w = is_write ? 1 : 0;
    hdr->a = 0;
    hdr->p = 0;
    hdr->c = 0;
    hdr->b = 0;
    hdr->r = 0;
    hdr->pmp = 0;
    hdr->prdtl = 1;
    hdr->prdbc = 0;

    ahci_memset(&g_cmd_table, 0, sizeof(g_cmd_table));

    uint64_t buf_addr = (uint64_t)(uintptr_t)buffer;
    g_cmd_table.prdt_entry[0].dba  = (uint32_t)(buf_addr & 0xFFFFFFFFu);
    g_cmd_table.prdt_entry[0].dbau = (uint32_t)(buf_addr >> 32);
    g_cmd_table.prdt_entry[0].dbc  = (uint32_t)(((uint32_t)sector_count * 512u) - 1u);
    g_cmd_table.prdt_entry[0].i    = 0;

    fis_reg_h2d_t *fis = (fis_reg_h2d_t *)&g_cmd_table.cfis[0];
    ahci_memset(fis, 0, sizeof(*fis));
    fis->fis_type = 0x27;
    fis->c = 1;
    fis->command = ata_command;

    fis->lba0 = (uint8_t)(lba & 0xFF);
    fis->lba1 = (uint8_t)((lba >> 8) & 0xFF);
    fis->lba2 = (uint8_t)((lba >> 16) & 0xFF);
    fis->device = (uint8_t)(1u << 6); /* LBA modu */
    fis->lba3 = (uint8_t)((lba >> 24) & 0xFF);
    fis->lba4 = (uint8_t)((lba >> 32) & 0xFF);
    fis->lba5 = (uint8_t)((lba >> 40) & 0xFF);

    fis->countl = (uint8_t)(sector_count & 0xFF);
    fis->counth = (uint8_t)((sector_count >> 8) & 0xFF);

    uint32_t spin = 0;
    while ((g_port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) != 0) {
        spin++;
        if (spin >= 5000000u) return AHCI_ERR_TIMEOUT;
    }

    g_port->ci = 1u;

    spin = 0;
    for (;;) {
        if (!(g_port->ci & 1u)) break;
        if (g_port->is & HBA_PxIS_TFES) return AHCI_ERR_TASK_FILE_ERROR;
        spin++;
        if (spin >= 50000000u) return AHCI_ERR_TIMEOUT;
    }

    if (g_port->is & HBA_PxIS_TFES) return AHCI_ERR_TASK_FILE_ERROR;

    return AHCI_OK;
}

static ahci_result_t ahci_identify(uint64_t *out_total_sectors) {
    ahci_result_t res = ahci_issue_command(0, 1, g_identify_buffer, ATA_CMD_IDENTIFY, false);
    if (res != AHCI_OK) return res;

    uint16_t *words = (uint16_t *)g_identify_buffer;
    /* ATA IDENTIFY: kelime 100-103 = LBA48 toplam sektor sayisi (64-bit) */
    uint64_t total = (uint64_t)words[100] | ((uint64_t)words[101] << 16) | ((uint64_t)words[102] << 32) | ((uint64_t)words[103] << 48);
    *out_total_sectors = total;
    return AHCI_OK;
}

ahci_result_t ahci_init(void) {
    pci_device_t dev;
    if (!pci_find_device_by_class(0x01, 0x06, 0x01, &dev)) {
        return AHCI_ERR_NO_CONTROLLER;
    }

    pci_enable_bus_mastering(&dev);

    uint64_t abar = pci_get_bar_address(&dev, 5);
    if (abar == 0) return AHCI_ERR_NO_CONTROLLER;

    if (!paging_identity_map_region(abar, 0x10000)) {
        return AHCI_ERR_TIMEOUT;
    }

    g_hba = (hba_mem_t *)(uintptr_t)abar;
    g_hba->ghc |= HBA_GHC_AE;

    uint32_t port_indices[AHCI_MAX_DISKS];
    uint32_t found = ahci_find_all_sata_ports(g_hba, port_indices, AHCI_MAX_DISKS);
    if (found == 0) {
        return AHCI_ERR_NO_DRIVE;
    }
    if (found > AHCI_MAX_DISKS) found = AHCI_MAX_DISKS;

    g_disk_count = 0;
    for (uint32_t i = 0; i < found; i++) {
        hba_port_t *port = hba_port_ptr(g_hba, port_indices[i]);

        if (ahci_port_rebase(port) != AHCI_OK) continue; /* bu disk atlanir, digerlerine devam */

        g_port = port; /* IDENTIFY icin gecici olarak bu portu "secili" yap */
        uint64_t sectors = 0;
        if (ahci_identify(&sectors) != AHCI_OK) sectors = 0;

        g_disks[g_disk_count].port_index = port_indices[i];
        g_disks[g_disk_count].port = port;
        g_disks[g_disk_count].total_sectors = sectors;
        g_disk_count++;
    }

    if (g_disk_count == 0) return AHCI_ERR_NO_DRIVE;

    g_port = g_disks[0].port;
    g_total_sectors = g_disks[0].total_sectors;

    return AHCI_OK;
}

uint32_t ahci_get_disk_count(void) {
    return g_disk_count;
}

bool ahci_get_disk_info(uint32_t disk_index, ahci_disk_info_t *out) {
    if (disk_index >= g_disk_count || out == NULL) return false;
    out->port_index = g_disks[disk_index].port_index;
    out->total_sectors = g_disks[disk_index].total_sectors;
    return true;
}

ahci_result_t ahci_select_disk(uint32_t disk_index) {
    if (disk_index >= g_disk_count) return AHCI_ERR_INVALID;
    g_port = g_disks[disk_index].port;
    g_total_sectors = g_disks[disk_index].total_sectors;
    return AHCI_OK;
}

ahci_result_t ahci_read_sectors(uint64_t lba, uint32_t sector_count, void *buffer) {
    if (sector_count == 0 || sector_count > 0xFFFF) return AHCI_ERR_INVALID;
    return ahci_issue_command(lba, (uint16_t)sector_count, buffer, ATA_CMD_READ_DMA_EX, false);
}

ahci_result_t ahci_write_sectors(uint64_t lba, uint32_t sector_count, const void *buffer) {
    if (sector_count == 0 || sector_count > 0xFFFF) return AHCI_ERR_INVALID;
    return ahci_issue_command(lba, (uint16_t)sector_count, (void *)buffer, ATA_CMD_WRITE_DMA_EX, true);
}

uint64_t ahci_get_total_sectors(void) {
    return g_total_sectors;
}