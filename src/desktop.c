#include "desktop.h"
#include "font.h"
#include "info.h"
#include "maxbash.h"
#include "filemanager.h"
#include "editor.h"
#include "power.h"

void desktop_init(void) {

}

void desktop_draw(BootInfo *boot_info) {
    fill_screen(boot_info, 0x001A252C);

    draw_rect(boot_info, 0, 0, boot_info->width, 35, 0x00111827);
    draw_string(boot_info, "VTX2 OS", 15, 10, COLOR_WHITE);

    draw_rect(boot_info, 120, 5, 80, 25, 0x002C3E50);
    draw_string(boot_info, "Bilgi", 140, 10, COLOR_WHITE);

    draw_rect(boot_info, 210, 5, 90, 25, 0x0027AE60);
    draw_string(boot_info, "MaxBash", 225, 10, COLOR_WHITE);

    draw_rect(boot_info, 310, 5, 130, 25, 0x00884EA0);
    draw_string(boot_info, "Dosyalar", 330, 10, COLOR_WHITE);

    draw_rect(boot_info, 450, 5, 130, 25, 0x00B8860B);
    draw_string(boot_info, "Not Defteri", 460, 10, COLOR_WHITE);

    draw_rect(boot_info, boot_info->width - 90, 5, 80, 25, COLOR_RED);
    draw_string(boot_info, "Kapat", boot_info->width - 75, 10, COLOR_WHITE);

    info_app_draw(boot_info);
    maxbash_app_draw(boot_info);
    filemanager_app_draw(boot_info);
    editor_app_draw(boot_info);
}

bool desktop_handle_click(BootInfo *boot_info, int32_t mx, int32_t my) {

    if (my >= 0 && my <= 35) {

        if (mx >= 120 && mx <= 200) {
            info_app_open();
            desktop_draw(boot_info);
            return true;
        }

        if (mx >= 210 && mx <= 300) {
            maxbash_app_open();
            desktop_draw(boot_info);
            return true;
        }

        if (mx >= 310 && mx <= 440) {
            filemanager_app_open();
            desktop_draw(boot_info);
            return true;
        }

        if (mx >= 450 && mx <= 580) {
            editor_app_open_new();
            desktop_draw(boot_info);
            return true;
        }

        if (mx >= (int32_t)(boot_info->width - 90) && mx <= (int32_t)(boot_info->width - 10)) {
            power_shutdown(boot_info);
            return true;
        }
    }

    bool changed = false;
    changed |= info_app_handle_click(boot_info, mx, my);
    changed |= maxbash_app_handle_click(boot_info, mx, my);
    changed |= filemanager_app_handle_click(boot_info, mx, my);
    changed |= editor_app_handle_click(boot_info, mx, my);
    if (changed) desktop_draw(boot_info);
    return changed;
}

bool desktop_handle_right_click(BootInfo *boot_info, int32_t mx, int32_t my) {
    bool changed = filemanager_app_handle_right_click(boot_info, mx, my);
    if (changed) desktop_draw(boot_info);
    return changed;
}

bool desktop_handle_pointer(BootInfo *boot_info, int32_t mx, int32_t my, bool left_down, bool left_pressed) {
    bool changed = false;
    changed |= info_app_handle_pointer(boot_info, mx, my, left_down, left_pressed);
    changed |= maxbash_app_handle_pointer(boot_info, mx, my, left_down, left_pressed);
    changed |= filemanager_app_handle_pointer(boot_info, mx, my, left_down, left_pressed);
    changed |= editor_app_handle_pointer(boot_info, mx, my, left_down, left_pressed);
    if (changed) desktop_draw(boot_info);
    return changed;
}

bool desktop_handle_scroll(BootInfo *boot_info, int32_t delta) {
    bool changed = false;
    changed |= maxbash_app_handle_scroll(delta);
    changed |= filemanager_app_handle_scroll(delta);
    changed |= editor_app_handle_scroll(delta);
    if (!changed) return false;
    desktop_draw(boot_info);
    return true;
}