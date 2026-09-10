#include "filemanager.h"
#include "window.h"
#include "font.h"
#include "kernel.h"
#include "vtx2fs_state.h"
#include "editor.h"
#include "pit.h"
#include <stdint.h>
#include <stdbool.h>

static Window fm_win = {
    .title = "Dosya Yoneticisi",
    .x = 150, .y = 60,
    .width = 520, .height = 380,
    .is_open = false,
    .header_color = 0x00884EA0
};

#define FM_MAX_VISIBLE_ENTRIES 200
#define FM_MAX_NAV_DEPTH 32
#define FM_VISIBLE_ROWS 12
#define FM_ROW_HEIGHT 16

typedef struct {
    uint64_t index;
    char name[VTX2_MAX_NAME_LEN];
    uint8_t type;
    uint64_t size_bytes;
} fm_row_t;

static uint64_t nav_stack[FM_MAX_NAV_DEPTH];
static uint32_t nav_depth = 0;
static uint64_t current_dir_index = 0;
static bool current_dir_known = false;

static fm_row_t rows[FM_MAX_VISIBLE_ENTRIES];
static uint64_t row_count = 0;
static uint32_t scroll_offset = 0;

static uint32_t new_folder_counter = 1;
static uint32_t new_file_counter = 1;

#define FM_DOUBLE_CLICK_TICKS 500

static int64_t selected_row = -1;
static uint64_t last_click_time = 0;
static int64_t last_click_row = -1;

#define CTX_ITEM_HEIGHT 20
#define CTX_MENU_WIDTH 120

typedef enum {
    CTX_MODE_ROW,
    CTX_MODE_EMPTY
} fm_ctx_mode_t;

static bool ctx_menu_open = false;
static fm_ctx_mode_t ctx_menu_mode = CTX_MODE_ROW;
static int32_t ctx_menu_x = 0, ctx_menu_y = 0;
static uint64_t ctx_menu_target_row = 0;

static void fm_uint_to_str(uint64_t value, char *out) {
    char tmp[24];
    int n = 0;
    if (value == 0) {
        tmp[n++] = '0';
    } else {
        while (value > 0 && n < 24) {
            tmp[n++] = (char)('0' + (value % 10));
            value /= 10;
        }
    }
    int k = 0;
    for (int i = n - 1; i >= 0; i--) out[k++] = tmp[i];
    out[k] = '\0';
}

static void fm_refresh(void) {
    row_count = 0;
    scroll_offset = 0;
    selected_row = -1;
    last_click_row = -1;
    ctx_menu_open = false;
    if (!current_dir_known) return;
    if (!vtx2fs_state_is_mounted()) return;

    vtx2fs_blockdev_t *dev = vtx2fs_state_get_device();
    vtx2_superblock_t *sb = vtx2fs_state_get_superblock();
    if (!dev || !sb) return;

    uint64_t indices[FM_MAX_VISIBLE_ENTRIES];
    uint64_t count = 0;
    vtx2fs_result_t res = vtx2_list_dir(dev, sb, current_dir_index, indices, FM_MAX_VISIBLE_ENTRIES, &count);
    if (res != VTX2_OK) return;

    uint64_t limit = count < FM_MAX_VISIBLE_ENTRIES ? count : FM_MAX_VISIBLE_ENTRIES;
    for (uint64_t i = 0; i < limit; i++) {
        vtx2_entry_t e;
        if (vtx2_stat(dev, sb, indices[i], &e) != VTX2_OK) continue;

        rows[row_count].index = indices[i];
        uint64_t j = 0;
        while (e.name[j] != '\0' && j + 1 < VTX2_MAX_NAME_LEN) { rows[row_count].name[j] = e.name[j]; j++; }
        rows[row_count].name[j] = '\0';
        rows[row_count].type = e.type;
        rows[row_count].size_bytes = e.size_bytes;
        row_count++;
    }
}

static void fm_enter_root_if_needed(void) {
    if (current_dir_known) return;
    vtx2_superblock_t *sb = vtx2fs_state_get_superblock();
    if (!sb) return;
    current_dir_index = sb->root_entry_index;
    current_dir_known = true;
    nav_depth = 0;
    fm_refresh();
}

void filemanager_app_init(void) {
    fm_win.is_open = false;
    fm_win.minimized = false;
    fm_win.maximized = false;
    current_dir_known = false;
    nav_depth = 0;
    row_count = 0;
    scroll_offset = 0;
    new_folder_counter = 1;
    new_file_counter = 1;
}

void filemanager_app_open(void) {
    if (fm_win.is_open && fm_win.minimized) {
        fm_win.minimized = false;
        return;
    }
    fm_win.is_open = true;
    fm_win.minimized = false;
    fm_enter_root_if_needed();
    fm_refresh();
}

void filemanager_app_draw(BootInfo *boot_info) {
    if (!fm_win.is_open || fm_win.minimized) return;

    window_draw(boot_info, &fm_win);
    draw_rect(boot_info, fm_win.x + 5, fm_win.y + 60, fm_win.width - 10, fm_win.height - 65, 0x00101418);

    if (!vtx2fs_state_is_mounted()) {
        draw_string(boot_info, "VTX2FS baglanamadi - disk/format sorunu var.", fm_win.x + 15, fm_win.y + 80, COLOR_RED);
        return;
    }

    fm_enter_root_if_needed();

    /* Ust arac cubugu */
    draw_rect(boot_info, fm_win.x + 10, fm_win.y + 35, 60, 20, 0x00333333);
    draw_string(boot_info, "Geri", fm_win.x + 20, fm_win.y + 39, COLOR_WHITE);

    draw_rect(boot_info, fm_win.x + 80, fm_win.y + 35, 110, 20, 0x00334455);
    draw_string(boot_info, "Yeni Klasor", fm_win.x + 88, fm_win.y + 39, COLOR_WHITE);

    draw_rect(boot_info, fm_win.x + 200, fm_win.y + 35, 100, 20, 0x00334455);
    draw_string(boot_info, "Yeni Dosya", fm_win.x + 208, fm_win.y + 39, COLOR_WHITE);

    uint32_t y = (uint32_t)fm_win.y + 65;
    uint32_t shown = 0;
    for (uint64_t i = scroll_offset; i < row_count && shown < FM_VISIBLE_ROWS; i++, shown++) {
        fm_row_t *r = &rows[i];
        uint32_t color = (r->type == VTX2_ENTRY_DIRECTORY) ? COLOR_YELLOW : COLOR_WHITE;

        if ((int64_t)i == selected_row) {
            draw_rect(boot_info, fm_win.x + 8, y - 2, fm_win.width - 16, FM_ROW_HEIGHT, 0x00334455);
        }

        char line[VTX2_MAX_NAME_LEN + 32];
        uint64_t p = 0;

        if (r->type == VTX2_ENTRY_DIRECTORY) {
            line[p++] = '[';
            for (uint64_t j = 0; r->name[j] != '\0' && p < sizeof(line) - 2; j++) line[p++] = r->name[j];
            line[p++] = ']';
        } else {
            for (uint64_t j = 0; r->name[j] != '\0' && p < sizeof(line) - 24; j++) line[p++] = r->name[j];
            line[p++] = ' ';
            line[p++] = '(';
            char sizestr[24];
            fm_uint_to_str(r->size_bytes, sizestr);
            for (uint64_t j = 0; sizestr[j] != '\0'; j++) line[p++] = sizestr[j];
            line[p++] = 'B';
            line[p++] = ')';
        }
        line[p] = '\0';

        draw_string(boot_info, line, (uint32_t)fm_win.x + 15, y, color);
        y += FM_ROW_HEIGHT;
    }

    if (row_count == 0) {
        draw_string(boot_info, "(bu dizin bos)", fm_win.x + 15, fm_win.y + 65, 0x00888888);
    }

    if (ctx_menu_open && (ctx_menu_mode == CTX_MODE_EMPTY || ctx_menu_target_row < row_count)) {
        draw_rect(boot_info, ctx_menu_x, ctx_menu_y, CTX_MENU_WIDTH, CTX_ITEM_HEIGHT * 2, 0x00222222);
        draw_rect(boot_info, ctx_menu_x, ctx_menu_y, CTX_MENU_WIDTH, 1, 0x00666666);
        draw_rect(boot_info, ctx_menu_x, ctx_menu_y + CTX_ITEM_HEIGHT, CTX_MENU_WIDTH, 1, 0x00444444);
        if (ctx_menu_mode == CTX_MODE_ROW) {
            draw_string(boot_info, "Ac", ctx_menu_x + 10, ctx_menu_y + 5, COLOR_WHITE);
            draw_string(boot_info, "Sil", ctx_menu_x + 10, ctx_menu_y + 5 + CTX_ITEM_HEIGHT, COLOR_RED);
        } else {
            draw_string(boot_info, "Yeni Klasor", ctx_menu_x + 10, ctx_menu_y + 5, COLOR_WHITE);
            draw_string(boot_info, "Yeni Dosya", ctx_menu_x + 10, ctx_menu_y + 5 + CTX_ITEM_HEIGHT, COLOR_WHITE);
        }
    }
}

static void fm_create_new_folder(void) {
    vtx2fs_blockdev_t *dev = vtx2fs_state_get_device();
    vtx2_superblock_t *sb = vtx2fs_state_get_superblock();
    if (!dev || !sb) return;

    char name[32];
    char numstr[24];
    fm_uint_to_str(new_folder_counter, numstr);
    uint64_t p = 0;
    const char *prefix = "Klasor";
    for (uint64_t i = 0; prefix[i]; i++) name[p++] = prefix[i];
    for (uint64_t i = 0; numstr[i]; i++) name[p++] = numstr[i];
    name[p] = '\0';

    uint64_t out_index;
    vtx2fs_result_t res = vtx2_mkdir(dev, sb, current_dir_index, name, &out_index);
    if (res == VTX2_OK) {
        new_folder_counter++;
        fm_refresh();
    }
}

static void fm_create_new_file(void) {
    vtx2fs_blockdev_t *dev = vtx2fs_state_get_device();
    vtx2_superblock_t *sb = vtx2fs_state_get_superblock();
    if (!dev || !sb) return;

    char name[32];
    char numstr[24];
    fm_uint_to_str(new_file_counter, numstr);
    uint64_t p = 0;
    const char *prefix = "Dosya";
    for (uint64_t i = 0; prefix[i]; i++) name[p++] = prefix[i];
    for (uint64_t i = 0; numstr[i]; i++) name[p++] = numstr[i];
    const char *suffix = ".txt";
    for (uint64_t i = 0; suffix[i]; i++) name[p++] = suffix[i];
    name[p] = '\0';

    uint64_t out_index;
    vtx2fs_result_t res = vtx2_create_file(dev, sb, current_dir_index, name, &out_index);
    if (res == VTX2_OK) {
        new_file_counter++;
        fm_refresh();
    }
}

bool filemanager_app_handle_click(BootInfo *boot_info, int32_t mx, int32_t my) {
    if (!fm_win.is_open) return false;

    if (ctx_menu_open) {
        int32_t local_x = mx - ctx_menu_x;
        int32_t local_y = my - ctx_menu_y;
        bool inside = (local_x >= 0 && local_x <= CTX_MENU_WIDTH && local_y >= 0 && local_y <= CTX_ITEM_HEIGHT * 2);

        if (inside) {
            int32_t item = local_y / CTX_ITEM_HEIGHT;

            if (ctx_menu_mode == CTX_MODE_ROW && ctx_menu_target_row < row_count) {
                fm_row_t *r = &rows[ctx_menu_target_row];
                if (item == 0) {
                    /* Ac */
                    if (r->type == VTX2_ENTRY_DIRECTORY && nav_depth < FM_MAX_NAV_DEPTH) {
                        nav_stack[nav_depth++] = current_dir_index;
                        current_dir_index = r->index;
                        fm_refresh();
                    } else if (r->type == VTX2_ENTRY_FILE) {
                        editor_app_open_file(r->index, r->name);
                    }
                } else if (item == 1) {
                    vtx2fs_blockdev_t *dev = vtx2fs_state_get_device();
                    vtx2_superblock_t *sb = vtx2fs_state_get_superblock();
                    if (dev && sb) {
                        vtx2_delete(dev, sb, r->index);
                        fm_refresh();
                    }
                }
            } else if (ctx_menu_mode == CTX_MODE_EMPTY) {
                if (item == 0) {
                    fm_create_new_folder();
                } else if (item == 1) {
                    fm_create_new_file();
                }
            }
        }

        ctx_menu_open = false;
        return true;
    }

    if (window_is_close_clicked(&fm_win, mx, my)) {
        fm_win.is_open = false;
        fm_win.minimized = false;
        return true;
    }
    if (window_is_minimize_clicked(&fm_win, mx, my)) {
        fm_win.minimized = true;
        return true;
    }
    if (window_is_maximize_clicked(&fm_win, mx, my)) {
        window_toggle_maximize(boot_info, &fm_win);
        return true;
    }

    if (!vtx2fs_state_is_mounted()) return false;

    int32_t rel_x = mx - fm_win.x;
    int32_t rel_y = my - fm_win.y;

    if (rel_x >= 10 && rel_x <= 70 && rel_y >= 35 && rel_y <= 55) {
        if (nav_depth > 0) {
            nav_depth--;
            current_dir_index = nav_stack[nav_depth];
            fm_refresh();
        }
        return true;
    }

    if (rel_x >= 80 && rel_x <= 190 && rel_y >= 35 && rel_y <= 55) {
        fm_create_new_folder();
        return true;
    }

    if (rel_x >= 200 && rel_x <= 300 && rel_y >= 35 && rel_y <= 55) {
        fm_create_new_file();
        return true;
    }

    if (rel_y >= 65) {
        uint32_t row_in_view = (uint32_t)((rel_y - 65) / FM_ROW_HEIGHT);
        uint64_t clicked_index = scroll_offset + row_in_view;
        if (clicked_index < row_count) {
            uint64_t now = pit_get_ticks();
            bool is_double_click = (last_click_row == (int64_t)clicked_index) && (now - last_click_time <= FM_DOUBLE_CLICK_TICKS);

            selected_row = (int64_t)clicked_index;
            last_click_row = (int64_t)clicked_index;
            last_click_time = now;

            if (is_double_click) {
                fm_row_t *r = &rows[clicked_index];
                if (r->type == VTX2_ENTRY_DIRECTORY && nav_depth < FM_MAX_NAV_DEPTH) {
                    nav_stack[nav_depth++] = current_dir_index;
                    current_dir_index = r->index;
                    fm_refresh();
                } else if (r->type == VTX2_ENTRY_FILE) {
                    editor_app_open_file(r->index, r->name);
                    last_click_row = -1;
                }
            }
            return true;
        }
    }

    return false;
}

bool filemanager_app_handle_right_click(BootInfo *boot_info, int32_t mx, int32_t my) {
    if (!fm_win.is_open || fm_win.minimized) return false;
    if (!vtx2fs_state_is_mounted()) return false;

    int32_t rel_x = mx - fm_win.x;
    int32_t rel_y = my - fm_win.y;
    if (rel_x < 0 || rel_x >= (int32_t)fm_win.width || rel_y < 65 || rel_y >= (int32_t)fm_win.height) return false;

    uint32_t row_in_view = (uint32_t)((rel_y - 65) / FM_ROW_HEIGHT);
    uint64_t clicked_index = scroll_offset + row_in_view;

    if (clicked_index < row_count) {
        selected_row = (int64_t)clicked_index;
        ctx_menu_mode = CTX_MODE_ROW;
        ctx_menu_target_row = clicked_index;
    } else {
        ctx_menu_mode = CTX_MODE_EMPTY;
    }

    ctx_menu_open = true;
    ctx_menu_x = mx;
    ctx_menu_y = my;

    if (ctx_menu_x + CTX_MENU_WIDTH > (int32_t)boot_info->width) {
        ctx_menu_x = (int32_t)boot_info->width - CTX_MENU_WIDTH - 2;
    }
    if (ctx_menu_y + CTX_ITEM_HEIGHT * 2 > (int32_t)boot_info->height) {
        ctx_menu_y = (int32_t)boot_info->height - CTX_ITEM_HEIGHT * 2 - 2;
    }

    return true;
}

bool filemanager_app_handle_pointer(BootInfo *boot_info, int32_t mx, int32_t my, bool left_down, bool left_pressed) {
    return window_handle_drag(boot_info, &fm_win, mx, my, left_down, left_pressed);
}

bool filemanager_app_handle_scroll(int32_t delta) {
    if (!fm_win.is_open || delta == 0) return false;

    int32_t next = (int32_t)scroll_offset + delta;
    if (next < 0) next = 0;

    uint64_t max_offset = row_count > FM_VISIBLE_ROWS ? row_count - FM_VISIBLE_ROWS : 0;
    if ((uint64_t)next > max_offset) next = (int32_t)max_offset;

    if ((uint64_t)next == scroll_offset) return false;
    scroll_offset = (uint32_t)next;
    return true;
}