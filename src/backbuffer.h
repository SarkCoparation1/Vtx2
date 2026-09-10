#ifndef BACKBUFFER_H
#define BACKBUFFER_H

#include "kernel.h"
#include <stdbool.h>

bool backbuffer_init(BootInfo *real_boot_info);
BootInfo *backbuffer_get(void);
void backbuffer_present(BootInfo *real_boot_info);
void backbuffer_present_rect(BootInfo *real_boot_info, int32_t x, int32_t y, uint32_t w, uint32_t h);

#endif