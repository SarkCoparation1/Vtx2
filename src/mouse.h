#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>
#include <stdbool.h>
#include "idt.h"
#include "kernel.h"

typedef struct {
    int32_t x;
    int32_t y;
    int32_t scroll;
    bool left_button;
    bool right_button;
    bool middle_button;
    bool is_imps2;
} MouseState;

void mouse_init(BootInfo *boot_info);
__attribute__((interrupt)) void mouse_handler(interrupt_frame_t *frame);
void mouse_draw_cursor(BootInfo *boot_info);
void mouse_erase_cursor(BootInfo *boot_info);
bool mouse_has_event(void);

typedef enum {
    MOUSE_UPDATE_NONE = 0,
    MOUSE_UPDATE_CURSOR_ONLY,
    MOUSE_UPDATE_FULL_REDRAW
} mouse_update_result_t;

mouse_update_result_t mouse_update(BootInfo *boot_info);
void mouse_get_dirty_rect(int32_t *out_x, int32_t *out_y, uint32_t *out_w, uint32_t *out_h);
MouseState* mouse_get_state(void);

#endif