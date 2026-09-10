#include "vtx2fs_ahci_bridge.h"
#include "ahci.h"
#include <stddef.h>

static vtx2fs_result_t bridge_read_block(void *device_context, uint64_t block_index, uint32_t block_size, void *out_buffer) {
    (void)device_context;

    if (block_size == 0 || (block_size % 512) != 0) return VTX2_ERR_INVALID;

    uint64_t byte_offset = block_index * (uint64_t)block_size;
    uint64_t lba = byte_offset / 512;
    uint32_t sector_count = block_size / 512;

    ahci_result_t res = ahci_read_sectors(lba, sector_count, out_buffer);
    return (res == AHCI_OK) ? VTX2_OK : VTX2_ERR_IO;
}

static vtx2fs_result_t bridge_write_block(void *device_context, uint64_t block_index, uint32_t block_size, const void *in_buffer) {
    (void)device_context;

    if (block_size == 0 || (block_size % 512) != 0) return VTX2_ERR_INVALID;

    uint64_t byte_offset = block_index * (uint64_t)block_size;
    uint64_t lba = byte_offset / 512;
    uint32_t sector_count = block_size / 512;

    ahci_result_t res = ahci_write_sectors(lba, sector_count, in_buffer);
    return (res == AHCI_OK) ? VTX2_OK : VTX2_ERR_IO;
}

void vtx2fs_ahci_bridge_init(vtx2fs_blockdev_t *out_dev) {
    out_dev->device_context = NULL; /* ahci.c kendi global durumunu tutuyor, ek baglama gerekmiyor */
    out_dev->read_block = bridge_read_block;
    out_dev->write_block = bridge_write_block;
}