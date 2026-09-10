#ifndef CONSOLE_H
#define CONSOLE_H

#include "kernel.h"

void console_set_target(BootInfo *target);
void console_print(const char *str, uint32_t x, uint32_t y, uint32_t color);

#endif