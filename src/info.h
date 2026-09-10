#ifndef INFO_H
#define INFO_H

#include "kernel.h"

void info_app_init(void);
void info_app_draw(BootInfo *boot_info);
bool info_app_handle_click(BootInfo *boot_info, int32_t mx, int32_t my);
bool info_app_handle_pointer(BootInfo *boot_info, int32_t mx, int32_t my, bool left_down, bool left_pressed);
void info_app_open(void);

#endif
