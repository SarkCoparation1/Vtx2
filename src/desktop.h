#ifndef DESKTOP_H
#define DESKTOP_H

#include "kernel.h"

void desktop_init(void);
void desktop_draw(BootInfo *boot_info);
bool desktop_handle_click(BootInfo *boot_info, int32_t mx, int32_t my);
bool desktop_handle_right_click(BootInfo *boot_info, int32_t mx, int32_t my);
bool desktop_handle_pointer(BootInfo *boot_info, int32_t mx, int32_t my, bool left_down, bool left_pressed);
bool desktop_handle_scroll(BootInfo *boot_info, int32_t delta);

#endif