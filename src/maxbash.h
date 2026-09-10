#ifndef MAXBASH_H
#define MAXBASH_H

#include "kernel.h"
#include <stdint.h>

void maxbash_app_init(void);
void maxbash_app_draw(BootInfo *boot_info);
bool maxbash_app_handle_click(BootInfo *boot_info, int32_t mx, int32_t my);
bool maxbash_app_handle_pointer(BootInfo *boot_info, int32_t mx, int32_t my, bool left_down, bool left_pressed);
bool maxbash_app_handle_key(char key);
bool maxbash_app_handle_scroll(int32_t delta);
void maxbash_app_open(void);

#endif
