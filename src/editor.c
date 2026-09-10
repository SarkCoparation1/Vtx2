#include "editor.h"
#include "window.h"
#include "font.h"
#include "kernel.h"
#include "vtx2fs_state.h"
#include <stdint.h>
#include <stdbool.h>

static Window editor_win = {
    .title = "VTX2 Not Defteri",
    .x = 100, .y = 50,
    .width = 600, .height = 420,
    .is_open = false,
    .header_color = 0x00B8860B
};

#define EDITOR_MAX_CHARS 8192
#define EDITOR_VISIBLE_LINES 16
#define EDITOR_LINE_HEIGHT 15
#define EDITOR_FILENAME_MAX 32
#define EDITOR_MAX_VLINES 512

static char text_buffer[EDITOR_MAX_CHARS];
static uint32_t text_length = 0;
static bool dirty = false;

static bool has_file = false;
static uint64_t file_entry_index = 0;
static char file_name[VTX2_MAX_NAME_LEN];

static bool awaiting_filename = false;
static char filename_input[EDITOR_FILENAME_MAX];
static uint32_t filename_input_length = 0;

static uint32_t scroll_offset = 0;

static uint32_t vline_start[EDITOR_MAX_VLINES];
static uint32_t vline_len[EDITOR_MAX_VLINES];

static void editor_clear_buffer(void) {
    text_length = 0;
    text_buffer[0] = '\0';
    dirty = false;
}

void editor_app_init(void) {
    editor_win.is_open = false;
    editor_win.minimized = false;
    editor_win.maximized = false;
    editor_clear_buffer();
    has_file = false;
    file_name[0] = '\0';
    awaiting_filename = false;
    filename_input_length = 0;
    scroll_offset = 0;
}

void editor_app_open_new(void) {
    if (editor_win.is_open && editor_win.minimized) {
        editor_win.minimized = false;
        return;
    }
    editor_win.is_open = true;
    editor_win.minimized = false;
    editor_clear_buffer();
    has_file = false;
    file_name[0] = '\0';
    awaiting_filename = false;
    scroll_offset = 0;
}

void editor_app_open_file(uint64_t entry_index, const char *name) {
    editor_win.is_open = true;
    editor_win.minimized = false;
    editor_clear_buffer();
    has_file = true;
    file_entry_index = entry_index;

    uint32_t i = 0;
    while (name[i] != '\0' && i + 1 < VTX2_MAX_NAME_LEN) { file_name[i] = name[i]; i++; }
    file_name[i] = '\0';

    awaiting_filename = false;
    scroll_offset = 0;

    if (!vtx2fs_state_is_mounted()) return;
    vtx2fs_blockdev_t *dev = vtx2fs_state_get_device();
    vtx2_superblock_t *sb = vtx2fs_state_get_superblock();
    if (!dev || !sb) return;

    uint64_t read_count = 0;
    vtx2fs_result_t res = vtx2_read(dev, sb, entry_index, 0, text_buffer, EDITOR_MAX_CHARS - 1, &read_count);
    if (res == VTX2_OK) {
        text_length = (uint32_t)read_count;
        text_buffer[text_length] = '\0';
    }
    dirty = false;
}

static void editor_save_to_attached_file(void) {
    if (!vtx2fs_state_is_mounted()) return;
    vtx2fs_blockdev_t *dev = vtx2fs_state_get_device();
    vtx2_superblock_t *sb = vtx2fs_state_get_superblock();
    if (!dev || !sb) return;

    uint64_t written = 0;
    vtx2fs_result_t res = vtx2_write(dev, sb, file_entry_index, 0, text_buffer, text_length, &written);
    if (res != VTX2_OK) return;

    vtx2_truncate(dev, sb, file_entry_index, text_length);
    dirty = false;
}

static uint32_t editor_build_vlines(uint32_t chars_per_line) {
    uint32_t vline_count = 0;
    uint32_t line_start = 0;

    for (uint32_t i = 0; i <= text_length; i++) {
        bool end_of_logical = (i == text_length) || (text_buffer[i] == '\n');
        if (!end_of_logical) continue;

        uint32_t seg_len = i - line_start;
        if (seg_len == 0) {
            if (vline_count < EDITOR_MAX_VLINES) {
                vline_start[vline_count] = line_start;
                vline_len[vline_count] = 0;
                vline_count++;
            }
        } else {
            uint32_t pos = line_start;
            while (pos < i) {
                uint32_t remaining = i - pos;
                uint32_t take = remaining > chars_per_line ? chars_per_line : remaining;
                if (vline_count < EDITOR_MAX_VLINES) {
                    vline_start[vline_count] = pos;
                    vline_len[vline_count] = take;
                    vline_count++;
                }
                pos += take;
            }
        }
        line_start = i + 1;
    }
    return vline_count;
}

void editor_app_draw(BootInfo *boot_info) {
    if (!editor_win.is_open || editor_win.minimized) return;

    window_draw(boot_info, &editor_win);
    draw_rect(boot_info, editor_win.x + 5, editor_win.y + 60, editor_win.width - 10, editor_win.height - 65, 0x000A0A0A);

    draw_rect(boot_info, editor_win.x + 10, editor_win.y + 35, 60, 20, 0x00333333);
    draw_string(boot_info, "Yeni", editor_win.x + 20, editor_win.y + 39, COLOR_WHITE);

    draw_rect(boot_info, editor_win.x + 80, editor_win.y + 35, 70, 20, 0x00445566);
    draw_string(boot_info, "Kaydet", editor_win.x + 88, editor_win.y + 39, COLOR_WHITE);

    char status[VTX2_MAX_NAME_LEN + 16];
    uint32_t p = 0;
    if (has_file) {
        for (uint32_t i = 0; file_name[i] != '\0' && p < sizeof(status) - 4; i++) status[p++] = file_name[i];
    } else {
        const char *s = "(yeni dosya)";
        for (uint32_t i = 0; s[i]; i++) status[p++] = s[i];
    }
    if (dirty) { status[p++] = ' '; status[p++] = '*'; }
    status[p] = '\0';
    draw_string(boot_info, status, editor_win.x + 160, editor_win.y + 39, dirty ? COLOR_YELLOW : COLOR_WHITE);

    if (!vtx2fs_state_is_mounted()) {
        draw_string(boot_info, "VTX2FS baglanamadi - kaydetme calismayacak.", editor_win.x + 15, editor_win.y + 68, COLOR_RED);
    }

    if (awaiting_filename) {
        draw_string(boot_info, "Kaydedilecek dosya adi (Enter=onayla, ESC=iptal):", editor_win.x + 15, editor_win.y + editor_win.height - 45, COLOR_GREEN);
        draw_string(boot_info, filename_input, editor_win.x + 15, editor_win.y + editor_win.height - 28, COLOR_WHITE);
        draw_string(boot_info, "_", editor_win.x + 15 + (int32_t)filename_input_length * 8, editor_win.y + editor_win.height - 28, COLOR_GREEN);
        return;
    }

    uint32_t chars_per_line = ((uint32_t)editor_win.width - 30) / 8;
    if (chars_per_line == 0) chars_per_line = 1;
    if (chars_per_line > 120) chars_per_line = 120;

    uint32_t total = editor_build_vlines(chars_per_line);

    uint32_t visible = EDITOR_VISIBLE_LINES;
    uint32_t end = total > scroll_offset ? total - scroll_offset : 0;
    uint32_t start = end > visible ? end - visible : 0;

    uint32_t y = (uint32_t)editor_win.y + 68;
    for (uint32_t vi = start; vi < end; vi++) {
        char linebuf[128];
        uint32_t len = vline_len[vi];
        if (len > sizeof(linebuf) - 1) len = sizeof(linebuf) - 1;
        for (uint32_t c = 0; c < len; c++) linebuf[c] = text_buffer[vline_start[vi] + c];
        linebuf[len] = '\0';
        draw_string(boot_info, linebuf, editor_win.x + 15, y, COLOR_WHITE);
        y += EDITOR_LINE_HEIGHT;
    }

    if (scroll_offset == 0 && total > 0) {
        uint32_t last = total - 1;
        int32_t cursor_x = editor_win.x + 15 + (int32_t)vline_len[last] * 8;
        int32_t cursor_y = editor_win.y + 68 + (int32_t)(end - start - 1) * EDITOR_LINE_HEIGHT;
        draw_string(boot_info, "_", cursor_x, cursor_y, COLOR_GREEN);
    }
}

bool editor_app_handle_click(BootInfo *boot_info, int32_t mx, int32_t my) {
    if (!editor_win.is_open) return false;

    if (window_is_close_clicked(&editor_win, mx, my)) {
        editor_win.is_open = false;
        editor_win.minimized = false;
        return true;
    }
    if (window_is_minimize_clicked(&editor_win, mx, my)) {
        editor_win.minimized = true;
        return true;
    }
    if (window_is_maximize_clicked(&editor_win, mx, my)) {
        window_toggle_maximize(boot_info, &editor_win);
        return true;
    }

    int32_t rel_x = mx - editor_win.x;
    int32_t rel_y = my - editor_win.y;

    if (rel_x >= 10 && rel_x <= 70 && rel_y >= 35 && rel_y <= 55) {
        editor_app_open_new();
        return true;
    }

    if (rel_x >= 80 && rel_x <= 150 && rel_y >= 35 && rel_y <= 55) {
        if (has_file) {
            editor_save_to_attached_file();
        } else {
            awaiting_filename = true;
            filename_input_length = 0;
            filename_input[0] = '\0';
        }
        return true;
    }

    return false;
}

bool editor_app_handle_pointer(BootInfo *boot_info, int32_t mx, int32_t my, bool left_down, bool left_pressed) {
    return window_handle_drag(boot_info, &editor_win, mx, my, left_down, left_pressed);
}

bool editor_app_handle_key(char key) {
    if (!editor_win.is_open) return false;

    if (awaiting_filename) {
        if (key == 27) {
            awaiting_filename = false;
            return true;
        }
        if (key == '\n') {
            if (filename_input_length == 0) return true;
            if (vtx2fs_state_is_mounted()) {
                vtx2fs_blockdev_t *dev = vtx2fs_state_get_device();
                vtx2_superblock_t *sb = vtx2fs_state_get_superblock();
                if (dev && sb) {
                    uint64_t new_index;
                    vtx2fs_result_t res = vtx2_create_file(dev, sb, sb->root_entry_index, filename_input, &new_index);
                    if (res == VTX2_OK) {
                        has_file = true;
                        file_entry_index = new_index;
                        uint32_t i = 0;
                        while (filename_input[i] != '\0' && i + 1 < VTX2_MAX_NAME_LEN) {
                            file_name[i] = filename_input[i];
                            i++;
                        }
                        file_name[i] = '\0';
                        editor_save_to_attached_file();
                    }
                }
            }
            awaiting_filename = false;
            return true;
        }
        if (key == '\b') {
            if (filename_input_length > 0) filename_input[--filename_input_length] = '\0';
            return true;
        }
        if (key >= 32 && key < 127 && filename_input_length + 1 < EDITOR_FILENAME_MAX) {
            filename_input[filename_input_length++] = key;
            filename_input[filename_input_length] = '\0';
        }
        return true;
    }

    if (key == '\b') {
        if (text_length > 0) {
            text_length--;
            text_buffer[text_length] = '\0';
            dirty = true;
        }
        return true;
    }
    if (key == '\n') {
        if (text_length + 1 < EDITOR_MAX_CHARS) {
            text_buffer[text_length++] = '\n';
            text_buffer[text_length] = '\0';
            dirty = true;
        }
        return true;
    }
    if ((key >= 32 && key < 127) && text_length + 1 < EDITOR_MAX_CHARS) {
        text_buffer[text_length++] = key;
        text_buffer[text_length] = '\0';
        dirty = true;
        return true;
    }

    return false;
}

bool editor_app_handle_scroll(int32_t delta) {
    if (!editor_win.is_open || delta == 0) return false;

    int32_t next = (int32_t)scroll_offset + delta;
    if (next < 0) next = 0;
    if (next > 100000) next = 100000;

    if ((uint32_t)next == scroll_offset) return false;
    scroll_offset = (uint32_t)next;
    return true;
}