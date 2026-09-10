#ifndef AHCI_H
#define AHCI_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    AHCI_OK = 0,
    AHCI_ERR_NO_CONTROLLER,
    AHCI_ERR_NO_DRIVE,
    AHCI_ERR_TIMEOUT,
    AHCI_ERR_TASK_FILE_ERROR,
    AHCI_ERR_INVALID
} ahci_result_t;

#define AHCI_MAX_DISKS 8

typedef struct {
    uint32_t port_index;
    uint64_t total_sectors;
} ahci_disk_info_t;

ahci_result_t ahci_init(void);
uint32_t ahci_get_disk_count(void);
bool ahci_get_disk_info(uint32_t disk_index, ahci_disk_info_t *out);
ahci_result_t ahci_select_disk(uint32_t disk_index);
ahci_result_t ahci_read_sectors(uint64_t lba, uint32_t sector_count, void *buffer);
ahci_result_t ahci_write_sectors(uint64_t lba, uint32_t sector_count, const void *buffer);
uint64_t ahci_get_total_sectors(void);

#endif /* AHCI_H */