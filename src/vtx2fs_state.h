#ifndef VTX2FS_STATE_H
#define VTX2FS_STATE_H

#include "vtx2fs.h"
#include <stdbool.h>

void vtx2fs_state_init(void);
bool vtx2fs_state_is_mounted(void);
vtx2fs_blockdev_t *vtx2fs_state_get_device(void);
vtx2_superblock_t *vtx2fs_state_get_superblock(void);
const char *vtx2fs_state_get_status_message(void);

#endif