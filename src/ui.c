#include "ui.h"

static const char cursor_sprite[CURSOR_HEIGHT][CURSOR_WIDTH + 1] = {
    "100000000000",
    "110000000000",
    "121000000000",
    "122100000000",
    "122210000000",
    "122221000000",
    "122222100000",
    "122222210000",
    "122222221000",
    "122222222100",
    "122222222210",
    "122222111110",
    "122122100000",
    "121012210000",
    "110012210000",
    "100001221000",
    "000001221000",
    "000000110000",
    "000000000000"
};

static uint32_t saved_bg[CURSOR_WIDTH * CURSOR_HEIGHT];
static int32_t prev_x = -1;
static int32_t prev_y = -1;
static bool has_saved_bg = false;

void ui_init(BootInfo *boot_info) {
    prev_x = -1;
    prev_y = -1;
    has_saved_bg = false;
}

void ui_update_cursor(BootInfo *boot_info, int32_t new_x, int32_t new_y) {
    uint32_t *fb = (uint32_t*)boot_info->framebuffer;

    if (has_saved_bg) {
        for (int py = 0; py < CURSOR_HEIGHT; py++) {
            for (int px = 0; px < CURSOR_WIDTH; px++) {
                int target_x = prev_x + px;
                int target_y = prev_y + py;

                // Ekran sınırları içinde mi kontrol et
                if (target_x >= 0 && target_x < (int)boot_info->width &&
                    target_y >= 0 && target_y < (int)boot_info->height) {
                    
                    fb[target_y * boot_info->width + target_x] = saved_bg[py * CURSOR_WIDTH + px];
                }
            }
        }
    }

    for (int py = 0; py < CURSOR_HEIGHT; py++) {
        for (int px = 0; px < CURSOR_WIDTH; px++) {
            int target_x = new_x + px;
            int target_y = new_y + py;

            if (target_x >= 0 && target_x < (int)boot_info->width &&
                target_y >= 0 && target_y < (int)boot_info->height) {
                
                saved_bg[py * CURSOR_WIDTH + px] = fb[target_y * boot_info->width + target_x];
            }
        }
    }

    for (int py = 0; py < CURSOR_HEIGHT; py++) {
        for (int px = 0; px < CURSOR_WIDTH; px++) {
            int target_x = new_x + px;
            int target_y = new_y + py;

            if (target_x >= 0 && target_x < (int)boot_info->width &&
                target_y >= 0 && target_y < (int)boot_info->height) {

                char type = cursor_sprite[py][px];
                if (type == '1') {
                    fb[target_y * boot_info->width + target_x] = 0x00000000; // Siyah Sınır
                } else if (type == '2') {
                    fb[target_y * boot_info->width + target_x] = COLOR_WHITE; // Beyaz İç
                }
            }
        }
    }

    // Koordinatları güncelle
    prev_x = new_x;
    prev_y = new_y;
    has_saved_bg = true;
}