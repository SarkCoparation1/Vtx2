#include "vtx2fs_state.h"
#include "vtx2fs_ahci_bridge.h"
#include "ahci.h"
#include <stddef.h>

static vtx2fs_blockdev_t g_dev;
static vtx2_superblock_t g_sb;
static bool g_mounted = false;
static char g_status[128];

static void state_strcat(char *dst, uint64_t max, const char *src) {
    uint64_t len = 0;
    while (dst[len] != '\0' && len < max - 1) len++;
    uint64_t i = 0;
    while (src[i] != '\0' && len < max - 1) dst[len++] = src[i++];
    dst[len] = '\0';
}

static void state_append_uint(char *dst, uint64_t max, uint64_t value) {
    char tmp[24];
    int n = 0;
    if (value == 0) {
        tmp[n++] = '0';
    } else {
        while (value > 0 && n < 24) {
            tmp[n++] = (char)('0' + (value % 10));
            value /= 10;
        }
    }
    uint64_t len = 0;
    while (dst[len] != '\0' && len < max - 1) len++;
    for (int i = n - 1; i >= 0 && len < max - 1; i--) dst[len++] = tmp[i];
    dst[len] = '\0';
}

void vtx2fs_state_init(void) {
    g_mounted = false;
    g_status[0] = '\0';

    ahci_result_t ahci_res = ahci_init();
    if (ahci_res != AHCI_OK) {
        state_strcat(g_status, sizeof(g_status), "AHCI: baslatilamadi (kod ");
        state_append_uint(g_status, sizeof(g_status), (uint64_t)ahci_res);
        state_strcat(g_status, sizeof(g_status), ")");
        return;
    }

    uint64_t total_sectors = ahci_get_total_sectors();
    state_strcat(g_status, sizeof(g_status), "AHCI: disk bulundu, sektor=");
    state_append_uint(g_status, sizeof(g_status), total_sectors);

    vtx2fs_ahci_bridge_init(&g_dev);

    vtx2fs_result_t mres = vtx2_mount(&g_dev, &g_sb);

    if (mres == VTX2_ERR_NOT_FORMATTED && total_sectors > 0) {
        uint64_t total_blocks = total_sectors / (VTX2_DEFAULT_BLOCK_SIZE / 512);
        vtx2fs_result_t fres = vtx2_format(&g_dev, total_blocks, VTX2_DEFAULT_BLOCK_SIZE);
        if (fres == VTX2_OK) {
            mres = vtx2_mount(&g_dev, &g_sb);
        } else {
            mres = fres;
        }
    }

    state_strcat(g_status, sizeof(g_status), " | VTX2FS: ");
    if (mres == VTX2_OK) {
        state_strcat(g_status, sizeof(g_status), "mount basarili");
        g_mounted = true;
    } else {
        state_strcat(g_status, sizeof(g_status), "mount basarisiz (kod ");
        state_append_uint(g_status, sizeof(g_status), (uint64_t)mres);
        state_strcat(g_status, sizeof(g_status), ")");
        g_mounted = false;
    }
}

bool vtx2fs_state_is_mounted(void) {
    return g_mounted;
}

vtx2fs_blockdev_t *vtx2fs_state_get_device(void) {
    return g_mounted ? &g_dev : NULL;
}

vtx2_superblock_t *vtx2fs_state_get_superblock(void) {
    return g_mounted ? &g_sb : NULL;
}

const char *vtx2fs_state_get_status_message(void) {
    return g_status;
}