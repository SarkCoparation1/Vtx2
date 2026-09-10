#ifndef WINDOW_H
#define WINDOW_H

#include <stdint.h>
#include <stdbool.h>
#include "kernel.h"

typedef struct {
	const char *title;
	int32_t x, y;
	uint32_t width, height;
	bool is_open;
	uint32_t header_color;
	bool dragging;
	int32_t drag_offset_x, drag_offset_y;
	bool minimized;
	bool maximized;
	int32_t restore_x, restore_y;
	uint32_t restore_width, restore_height;
} Window;

void window_draw(BootInfo *boot_info, Window *win);
bool window_is_close_clicked(Window *win, int32_t mx, int32_t my);
bool window_is_minimize_clicked(Window *win, int32_t mx, int32_t my);
bool window_is_maximize_clicked(Window *win, int32_t mx, int32_t my);
void window_toggle_maximize(BootInfo *boot_info, Window *win);
void window_open_or_restore(Window *win);

bool window_handle_drag(BootInfo *boot_info, Window *win, int32_t mx, int32_t my, bool left_down, bool left_pressed);

#endif