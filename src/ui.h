#ifndef UI_H
#define UI_H

#include <stdint.h>
#include <stdbool.h>
#include "kernel.h"

#define CURSOR_WIDTH  12
#define CURSOR_HEIGHT 19

void ui_init(BootInfo *boot_info);
void ui_update_cursor(BootInfo *boot_info, int32_t new_x, int32_t new_y);

#endif