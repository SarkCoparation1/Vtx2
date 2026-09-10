#ifndef EDITOR_H
#define EDITOR_H

#include "kernel.h"
#include <stdint.h>
#include <stdbool.h>

void editor_app_init(void);
void editor_app_open_new(void);
void editor_app_open_file(uint64_t entry_index, const char *name);
void editor_app_draw(BootInfo *boot_info);
bool editor_app_handle_click(BootInfo *boot_info, int32_t mx, int32_t my);
bool editor_app_handle_pointer(BootInfo *boot_info, int32_t mx, int32_t my, bool left_down, bool left_pressed);
bool editor_app_handle_key(char key);
bool editor_app_handle_scroll(int32_t delta);

#endif