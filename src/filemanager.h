#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include "kernel.h"
#include <stdint.h>
#include <stdbool.h>

void filemanager_app_init(void);
void filemanager_app_open(void);
void filemanager_app_draw(BootInfo *boot_info);
bool filemanager_app_handle_click(BootInfo *boot_info, int32_t mx, int32_t my);
bool filemanager_app_handle_right_click(BootInfo *boot_info, int32_t mx, int32_t my);
bool filemanager_app_handle_pointer(BootInfo *boot_info, int32_t mx, int32_t my, bool left_down, bool left_pressed);
bool filemanager_app_handle_scroll(int32_t delta);

#endif