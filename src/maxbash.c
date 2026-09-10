#include "maxbash.h"
#include "window.h"
#include "font.h"
#include "kernel.h"
#include <stdint.h>
#include <stdbool.h>

static Window bash_win = {
    .title = "MaxBash Terminal v1.0",
    .x = 80, .y = 80,
    .width = 550, .height = 320,
    .is_open = false,
    .header_color = 0x0027AE60
};

#define COMMAND_MAX 80
#define HISTORY_MAX 24
static char command[COMMAND_MAX];
static uint32_t command_length = 0;
static char history[HISTORY_MAX][COMMAND_MAX];
static uint32_t history_count = 0;
static uint32_t scroll_offset = 0;

static void copy_text(char *dst, const char *src) {
    uint32_t i = 0;
    while (src[i] && i + 1 < COMMAND_MAX) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static bool starts_with(const char *text, const char *prefix) {
    while (*prefix) if (*text++ != *prefix++) return false;
    return true;
}

static void history_add(const char *text) {
    if (history_count < HISTORY_MAX) {
        copy_text(history[history_count++], text);
    } else {
        for (uint32_t i = 1; i < HISTORY_MAX; i++) copy_text(history[i - 1], history[i]);
        copy_text(history[HISTORY_MAX - 1], text);
    }
}

void maxbash_app_init(void) {
	bash_win.is_open = false;
	bash_win.minimized = false;
	bash_win.maximized = false;
	command_length = 0;
	command[0] = '\0';
	history_count = 0;
	scroll_offset = 0;
}

void maxbash_app_open(void) {
	window_open_or_restore(&bash_win);
}

#define MB_MAX_VLINES 256
static char vline_buf[MB_MAX_VLINES][COMMAND_MAX + 2];
static uint32_t vline_count = 0;

static void mb_wrap_append(const char *text, uint32_t chars_per_line) {
    uint32_t len = 0;
    while (text[len] != '\0' && len < COMMAND_MAX + 20) len++;

    if (len == 0) {
        if (vline_count < MB_MAX_VLINES) { vline_buf[vline_count][0] = '\0'; vline_count++; }
        return;
    }

    uint32_t pos = 0;
    while (pos < len) {
        uint32_t remaining = len - pos;
        uint32_t take = remaining > chars_per_line ? chars_per_line : remaining;
        if (take > COMMAND_MAX + 1) take = COMMAND_MAX + 1;
        if (vline_count < MB_MAX_VLINES) {
            for (uint32_t c = 0; c < take; c++) vline_buf[vline_count][c] = text[pos + c];
            vline_buf[vline_count][take] = '\0';
            vline_count++;
        }
        pos += take;
    }
}

void maxbash_app_draw(BootInfo *boot_info) {
	if (!bash_win.is_open || bash_win.minimized) return;
	
	window_draw(boot_info, &bash_win);
	draw_rect(boot_info, bash_win.x + 5, bash_win.y + 35, bash_win.width - 10, bash_win.height - 40, 0x00050505);

    draw_string(boot_info, "VTX2 Shell (MaxBash)  |  help: commands  |  wheel: history", bash_win.x + 15, bash_win.y + 48, COLOR_GREEN);

    uint32_t chars_per_line = ((uint32_t)bash_win.width - 30) / 8;
    if (chars_per_line == 0) chars_per_line = 1;
    if (chars_per_line > COMMAND_MAX) chars_per_line = COMMAND_MAX;
    
    vline_count = 0;
    for (uint32_t i = 0; i < history_count; i++) {
        mb_wrap_append(history[i], chars_per_line);
    }

    uint32_t visible_lines = 20;
    uint32_t end = vline_count > scroll_offset ? vline_count - scroll_offset : 0;
    uint32_t start = end > visible_lines ? end - visible_lines : 0;
    uint32_t row = 0;
    for (uint32_t i = start; i < end; i++) {
        draw_string(boot_info, vline_buf[i], bash_win.x + 15, bash_win.y + 65 + row++ * 11, COLOR_WHITE);
    }

    uint32_t prompt_prefix_len = 13;
    uint32_t prompt_y = (uint32_t)bash_win.y + (uint32_t)bash_win.height - 28;

    if (prompt_prefix_len + command_length <= chars_per_line) {
        draw_string(boot_info, "root@vtx2:~# ", bash_win.x + 15, prompt_y, COLOR_GREEN);
        draw_string(boot_info, command, bash_win.x + 15 + (int32_t)prompt_prefix_len * 8, prompt_y, COLOR_WHITE);
        draw_string(boot_info, "_", bash_win.x + 15 + (int32_t)(prompt_prefix_len + command_length) * 8, prompt_y, COLOR_GREEN);
    } else {
        char full_line[13 + COMMAND_MAX];
        uint32_t p = 0;
        const char *prefix = "root@vtx2:~# ";
        for (uint32_t i = 0; prefix[i]; i++) full_line[p++] = prefix[i];
        for (uint32_t i = 0; command[i]; i++) full_line[p++] = command[i];
        full_line[p] = '\0';

        char prompt_vlines[8][COMMAND_MAX + 2];
        uint32_t prompt_vcount = 0;
        uint32_t len = p;
        uint32_t pos = 0;
        while (pos < len && prompt_vcount < 8) {
            uint32_t remaining = len - pos;
            uint32_t take = remaining > chars_per_line ? chars_per_line : remaining;
            for (uint32_t c = 0; c < take; c++) prompt_vlines[prompt_vcount][c] = full_line[pos + c];
            prompt_vlines[prompt_vcount][take] = '\0';
            prompt_vcount++;
            pos += take;
        }

        uint32_t base_y = prompt_y - (prompt_vcount - 1) * 11;
        for (uint32_t i = 0; i < prompt_vcount; i++) {
            draw_string(boot_info, prompt_vlines[i], bash_win.x + 15, base_y + i * 11, COLOR_WHITE);
        }
        uint32_t last_len = 0;
        while (prompt_vlines[prompt_vcount - 1][last_len] != '\0') last_len++;
        draw_string(boot_info, "_", bash_win.x + 15 + (int32_t)last_len * 8, base_y + (prompt_vcount - 1) * 11, COLOR_GREEN);
    }
}

bool maxbash_app_handle_click(BootInfo *boot_info, int32_t mx, int32_t my) {
    if (window_is_close_clicked(&bash_win, mx, my)) {
        bash_win.is_open = false;
        bash_win.minimized = false;
        return true;
    }
    if (window_is_minimize_clicked(&bash_win, mx, my)) {
        bash_win.minimized = true;
        return true;
    }
    if (window_is_maximize_clicked(&bash_win, mx, my)) {
        window_toggle_maximize(boot_info, &bash_win);
        return true;
    }
    return false;
}

bool maxbash_app_handle_pointer(BootInfo *boot_info, int32_t mx, int32_t my, bool left_down, bool left_pressed) {
    return window_handle_drag(boot_info, &bash_win, mx, my, left_down, left_pressed);
}

bool maxbash_app_handle_key(char key) {
    if (!bash_win.is_open) return false;
    if (key == '\b') {
        if (command_length > 0) command[--command_length] = '\0';
        return true;
    }
    if (key == '\n') {
        char line[COMMAND_MAX];
        copy_text(line, "root@vtx2:~# ");
        uint32_t pos = 13;
        for (uint32_t i = 0; command[i] && pos + 1 < COMMAND_MAX; i++) line[pos++] = command[i];
        line[pos] = '\0';
        history_add(line);
        if (starts_with(command, "help")) {
            history_add("commands: help, clear, echo <text>, ls, about");
        } else if (starts_with(command, "clear")) {
            history_count = 0;
            scroll_offset = 0;
        } else if (starts_with(command, "echo ")) {
            history_add(command + 5);
        } else if (starts_with(command, "ls")) {
            history_add("bin  dev  home  system  tmp");
        } else if (starts_with(command, "about")) {
            history_add("VTX2 OS / MaxBash v1.0");
        } else if (command_length) {
            history_add("maxbash: command not found");
        }
        command_length = 0;
        command[0] = '\0';
        return true;
    }
    if (key >= 32 && key < 127 && command_length + 1 < COMMAND_MAX) {
        command[command_length++] = key;
        command[command_length] = '\0';
        return true;
    }
    return false;
}

bool maxbash_app_handle_scroll(int32_t delta) {
    if (!bash_win.is_open || delta == 0) return false;
    int32_t next = (int32_t)scroll_offset + delta;
    if (next < 0) next = 0;
    uint32_t max_offset = history_count > 1 ? history_count - 1 : 0;
    if ((uint32_t)next > max_offset) next = (int32_t)max_offset;
    if ((uint32_t)next == scroll_offset) return false;
    scroll_offset = (uint32_t)next;
    return true;
}