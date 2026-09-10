#include "info.h"
#include "window.h"
#include "font.h"
#include "kernel.h"
#include <stdbool.h>

static Window info_win = {
	.title = "Sistem Bilgisi",
	.x = 100, .y = 80,
	.width = 450, .height = 250,
	.is_open = false,
	.header_color = 0x002C3E50
};

void info_app_init(void) {
	info_win.is_open = false;
	info_win.minimized = false;
	info_win.maximized = false;
}

void info_app_open(void) {
	window_open_or_restore(&info_win);
}

void info_app_draw(BootInfo *boot_info) {
	if (!info_win.is_open || info_win.minimized) return;
	
	window_draw(boot_info, &info_win);
	
	draw_string(boot_info, "OS Name    : VTX2 OS (x64)", info_win.x + 20, info_win.y + 50, COLOR_WHITE);
    draw_string(boot_info, "Kernel Ver : v0.50 Modular UI", info_win.x + 20, info_win.y + 80, COLOR_WHITE);
    draw_string(boot_info, "Resolution : 1024x768 (GOP Mode)", info_win.x + 20, info_win.y + 110, COLOR_YELLOW);
    draw_string(boot_info, "Mouse      : IRQ12 Interrupt Active", info_win.x + 20, info_win.y + 140, COLOR_GREEN);
    draw_string(boot_info, "Keyboard   : IRQ1 TR-Q Active", info_win.x + 20, info_win.y + 170, COLOR_GREEN);
}

bool info_app_handle_click(BootInfo *boot_info, int32_t mx, int32_t my) {
    if (window_is_close_clicked(&info_win, mx, my)) {
        info_win.is_open = false;
        info_win.minimized = false;
        return true;
    }
    if (window_is_minimize_clicked(&info_win, mx, my)) {
        info_win.minimized = true;
        return true;
    }
    if (window_is_maximize_clicked(&info_win, mx, my)) {
        window_toggle_maximize(boot_info, &info_win);
        return true;
    }
    return false;
}

bool info_app_handle_pointer(BootInfo *boot_info, int32_t mx, int32_t my, bool left_down, bool left_pressed) {
    return window_handle_drag(boot_info, &info_win, mx, my, left_down, left_pressed);
}