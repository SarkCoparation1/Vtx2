#include "window.h"
#include "font.h"
#include "ui.h"

#define WIN_BTN_SIZE 24
#define WIN_BTN_GAP 26

void window_draw(BootInfo *boot_info, Window *win) {
    if (!win->is_open) return;

    draw_rect(boot_info, win->x, win->y, win->width, win->height, 0x001C2833);

    draw_rect(boot_info, win->x, win->y, win->width, 30, win->header_color);

    draw_string(boot_info, win->title, win->x + 10, win->y + 8, COLOR_WHITE);

    int32_t close_x = win->x + (int32_t)win->width - WIN_BTN_GAP;
    int32_t max_x   = close_x - WIN_BTN_GAP;
    int32_t min_x   = max_x - WIN_BTN_GAP;
    int32_t btn_y   = win->y + 3;

    draw_rect(boot_info, close_x, btn_y, WIN_BTN_SIZE, WIN_BTN_SIZE, COLOR_RED);
    draw_string(boot_info, "X", close_x + 8, btn_y + 4, COLOR_WHITE);

    draw_rect(boot_info, max_x, btn_y, WIN_BTN_SIZE, WIN_BTN_SIZE, 0x00445566);
    draw_rect(boot_info, max_x + 6, btn_y + 6, 12, 12, COLOR_WHITE);
    draw_rect(boot_info, max_x + 8, btn_y + 8, 8, 8, 0x00445566);

    draw_rect(boot_info, min_x, btn_y, WIN_BTN_SIZE, WIN_BTN_SIZE, 0x00445566);
    draw_rect(boot_info, min_x + 6, btn_y + 17, 12, 2, COLOR_WHITE);
}

bool window_is_close_clicked(Window *win, int32_t mx, int32_t my) {
    if (!win->is_open) return false;

    int32_t btn_x = win->x + (int32_t)win->width - WIN_BTN_GAP;
    int32_t btn_y = win->y + 3;

    return (mx >= btn_x && mx <= btn_x + WIN_BTN_SIZE && my >= btn_y && my <= btn_y + WIN_BTN_SIZE);
}

bool window_is_maximize_clicked(Window *win, int32_t mx, int32_t my) {
    if (!win->is_open) return false;

    int32_t btn_x = win->x + (int32_t)win->width - WIN_BTN_GAP * 2;
    int32_t btn_y = win->y + 3;

    return (mx >= btn_x && mx <= btn_x + WIN_BTN_SIZE && my >= btn_y && my <= btn_y + WIN_BTN_SIZE);
}

bool window_is_minimize_clicked(Window *win, int32_t mx, int32_t my) {
    if (!win->is_open) return false;

    int32_t btn_x = win->x + (int32_t)win->width - WIN_BTN_GAP * 3;
    int32_t btn_y = win->y + 3;

    return (mx >= btn_x && mx <= btn_x + WIN_BTN_SIZE && my >= btn_y && my <= btn_y + WIN_BTN_SIZE);
}

void window_toggle_maximize(BootInfo *boot_info, Window *win) {
    if (!win->maximized) {
        win->restore_x = win->x;
        win->restore_y = win->y;
        win->restore_width = win->width;
        win->restore_height = win->height;

        win->x = 0;
        win->y = 36;
        win->width = boot_info->width;
        win->height = boot_info->height - 36;
        win->maximized = true;
    } else {
        win->x = win->restore_x;
        win->y = win->restore_y;
        win->width = win->restore_width;
        win->height = win->restore_height;
        win->maximized = false;
    }
}

void window_open_or_restore(Window *win) {
    if (win->is_open && win->minimized) {
        win->minimized = false;
        return;
    }
    win->is_open = true;
    win->minimized = false;
}

bool window_handle_drag(BootInfo *boot_info, Window *win, int32_t mx, int32_t my, bool left_down, bool left_pressed) {
    if (!win->is_open || win->minimized) return false;
    if (win->maximized) return false;

    if (left_pressed && mx >= win->x && mx < win->x + (int32_t)win->width &&
        my >= win->y && my < win->y + 30 &&
        !window_is_close_clicked(win, mx, my) &&
        !window_is_maximize_clicked(win, mx, my) &&
        !window_is_minimize_clicked(win, mx, my)) {
        win->dragging = true;
        win->drag_offset_x = mx - win->x;
        win->drag_offset_y = my - win->y;
        return false;
    }

    if (!left_down) {
        win->dragging = false;
        return false;
    }

    if (win->dragging) {
        int32_t new_x = mx - win->drag_offset_x;
        int32_t new_y = my - win->drag_offset_y;
        if (new_x < 0) new_x = 0;
        if (new_y < 35) new_y = 35;
        if (new_x > (int32_t)boot_info->width - (int32_t)win->width) new_x = (int32_t)boot_info->width - (int32_t)win->width;
        if (new_y > (int32_t)boot_info->height - (int32_t)win->height) new_y = (int32_t)boot_info->height - (int32_t)win->height;
        if (new_x != win->x || new_y != win->y) {
            win->x = new_x;
            win->y = new_y;
            return true;
        }
    }
    return false;
}