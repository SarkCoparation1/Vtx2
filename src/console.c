#include "console.h"
#include <stddef.h>

static BootInfo *g_target = NULL;

void console_set_target(BootInfo *target) {
    g_target = target;
}

void console_print(const char *str, uint32_t x, uint32_t y, uint32_t color) {
    if (!g_target) return;
    draw_string(g_target, str, x, y, color);
}