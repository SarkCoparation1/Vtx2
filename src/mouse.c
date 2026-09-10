#include "mouse.h"
#include "irqctl.h"
#include "desktop.h"

#define MOUSE_DATA_PORT 0x60
#define MOUSE_STATUS_PORT 0x64
#define MOUSE_CMD_PORT 0x64

static MouseState g_mouse_state;
static uint8_t mouse_cycle = 0;
static uint8_t mouse_packet[4];
static bool g_mouse_updated = false;

#define CURSOR_WIDTH 13
#define CURSOR_HEIGHT 20
static uint32_t cursor_background[CURSOR_WIDTH * CURSOR_HEIGHT];
static int32_t cursor_x;
static int32_t cursor_y;
static bool cursor_visible = false;
static bool previous_left_button = false;
static bool previous_right_button = false;

static void restore_cursor_background(BootInfo *boot_info);

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void mouse_wait(uint8_t type) {
    uint32_t time_out = 100000;
    if (type == 0) {
        while (time_out--) {
            if ((inb(MOUSE_STATUS_PORT) & 1) == 1) return;
        }
    } else {
        while (time_out--) {
            if ((inb(MOUSE_STATUS_PORT) & 2) == 0) return;
        }
    }
}

static void mouse_write(uint8_t write) {
    mouse_wait(1);
    outb(MOUSE_CMD_PORT, 0xD4);
    mouse_wait(1);
    outb(MOUSE_DATA_PORT, write);
}

static uint8_t mouse_read(void) {
    mouse_wait(0);
    return inb(MOUSE_DATA_PORT);
}

void mouse_init(BootInfo *boot_info) {
    g_mouse_state.x = boot_info->width / 2;
    g_mouse_state.y = boot_info->height / 2;
    g_mouse_state.scroll = 0;
    g_mouse_state.left_button = false;
    g_mouse_state.right_button = false;
    g_mouse_state.middle_button = false;
    g_mouse_state.is_imps2 = false;
    previous_left_button = false;
    previous_right_button = false;

    mouse_wait(1);
    outb(MOUSE_CMD_PORT, 0xA8);

    mouse_wait(1);
    outb(MOUSE_CMD_PORT, 0x20);
    mouse_wait(0);
    uint8_t status = inb(MOUSE_DATA_PORT) | 2;
    mouse_wait(1);
    outb(MOUSE_CMD_PORT, 0x60);
    mouse_wait(1);
    outb(MOUSE_DATA_PORT, status);

    mouse_write(0xF3); mouse_read(); mouse_write(200); mouse_read();
    mouse_write(0xF3); mouse_read(); mouse_write(100); mouse_read();
    mouse_write(0xF3); mouse_read(); mouse_write(80);  mouse_read();

    mouse_write(0xF2);
    mouse_read();
    uint8_t device_id = mouse_read();
    if (device_id == 3 || device_id == 4) {
        g_mouse_state.is_imps2 = true;
    }

    mouse_write(0xF4);
    mouse_read();
}

__attribute__((interrupt)) void mouse_handler(interrupt_frame_t *frame) {
    (void)frame;
    uint8_t status = inb(MOUSE_STATUS_PORT);
    if (!(status & 0x20)) {
        irqctl_eoi(12);
        return;
    }

    uint8_t data = inb(MOUSE_DATA_PORT);
    uint8_t packet_limit = g_mouse_state.is_imps2 ? 4 : 3;

    mouse_packet[mouse_cycle++] = data;

    if (mouse_cycle >= packet_limit) {
        mouse_cycle = 0;

        if (!(mouse_packet[0] & 0x08)) {
            irqctl_eoi(12);
            return;
        }

        g_mouse_state.left_button   = (mouse_packet[0] & 0x01) != 0;
        g_mouse_state.right_button  = (mouse_packet[0] & 0x02) != 0;
        g_mouse_state.middle_button = (mouse_packet[0] & 0x04) != 0;

        int32_t rel_x = mouse_packet[1] - ((mouse_packet[0] << 4) & 0x100);
        int32_t rel_y = mouse_packet[2] - ((mouse_packet[0] << 3) & 0x100);

        g_mouse_state.x += rel_x;
        g_mouse_state.y -= rel_y;

        if (g_mouse_state.x < 0) g_mouse_state.x = 0;
        if (g_mouse_state.y < 0) g_mouse_state.y = 0;

        if (g_mouse_state.is_imps2) {
            int8_t scroll_raw = (int8_t)(mouse_packet[3] & 0x0F);
            if (scroll_raw & 0x08) scroll_raw |= 0xF0;
            g_mouse_state.scroll += scroll_raw;
        }

        g_mouse_updated = true;
    }

    irqctl_eoi(12);
}

bool mouse_has_event(void) {
    if (g_mouse_updated) {
        g_mouse_updated = false;
        return true;
    }
    return false;
}

mouse_update_result_t mouse_update(BootInfo *boot_info) {
    if (mouse_has_event()) {
        if (g_mouse_state.x > (int32_t)boot_info->width - CURSOR_WIDTH) g_mouse_state.x = (int32_t)boot_info->width - CURSOR_WIDTH;
        if (g_mouse_state.y > (int32_t)boot_info->height - CURSOR_HEIGHT) g_mouse_state.y = (int32_t)boot_info->height - CURSOR_HEIGHT;

        bool left_click = g_mouse_state.left_button && !previous_left_button;
        bool right_click = g_mouse_state.right_button && !previous_right_button;
        int32_t scroll_delta = g_mouse_state.scroll;
        g_mouse_state.scroll = 0;

        restore_cursor_background(boot_info);

        bool full_redraw = false;
        if (left_click) {
            full_redraw |= desktop_handle_click(boot_info, g_mouse_state.x, g_mouse_state.y);
        }
        if (right_click) {
            full_redraw |= desktop_handle_right_click(boot_info, g_mouse_state.x, g_mouse_state.y);
        }
        full_redraw |= desktop_handle_pointer(boot_info, g_mouse_state.x, g_mouse_state.y, g_mouse_state.left_button, left_click);
        full_redraw |= desktop_handle_scroll(boot_info, scroll_delta);

        mouse_draw_cursor(boot_info);
        previous_left_button = g_mouse_state.left_button;
        previous_right_button = g_mouse_state.right_button;

        return full_redraw ? MOUSE_UPDATE_FULL_REDRAW : MOUSE_UPDATE_CURSOR_ONLY;
    }
    return MOUSE_UPDATE_NONE;
}

static uint32_t get_pixel(BootInfo *boot_info, int32_t x, int32_t y) {
    uint64_t offset = ((uint64_t)y * boot_info->ppsl + (uint32_t)x) * boot_info->bytes_per_pixel;
    if (boot_info->bytes_per_pixel == 4) {
        return *(uint32_t *)(boot_info->framebuffer + offset);
    }
    return boot_info->framebuffer[offset] | ((uint32_t)boot_info->framebuffer[offset + 1] << 8) | ((uint32_t)boot_info->framebuffer[offset + 2] << 16);
}

static void restore_cursor_background(BootInfo *boot_info) {
    if (!cursor_visible) return;
    for (uint32_t y = 0; y < CURSOR_HEIGHT; y++) {
        for (uint32_t x = 0; x < CURSOR_WIDTH; x++) {
            put_pixel(boot_info, cursor_x + x, cursor_y + y, cursor_background[y * CURSOR_WIDTH + x]);
        }
    }
    cursor_visible = false;
}

void mouse_erase_cursor(BootInfo *boot_info) {
    restore_cursor_background(boot_info);
}

static int32_t dirty_x = 0, dirty_y = 0;
static uint32_t dirty_w = CURSOR_WIDTH, dirty_h = CURSOR_HEIGHT;

void mouse_draw_cursor(BootInfo *boot_info) {
    restore_cursor_background(boot_info);

    int32_t old_x = cursor_x;
    int32_t old_y = cursor_y;
    cursor_x = g_mouse_state.x;
    cursor_y = g_mouse_state.y;

    int32_t rx1 = old_x < cursor_x ? old_x : cursor_x;
    int32_t ry1 = old_y < cursor_y ? old_y : cursor_y;
    int32_t rx2 = (old_x > cursor_x ? old_x : cursor_x) + CURSOR_WIDTH;
    int32_t ry2 = (old_y > cursor_y ? old_y : cursor_y) + CURSOR_HEIGHT;
    dirty_x = rx1;
    dirty_y = ry1;
    dirty_w = (uint32_t)(rx2 - rx1);
    dirty_h = (uint32_t)(ry2 - ry1);

    for (uint32_t y = 0; y < CURSOR_HEIGHT; y++) {
        for (uint32_t x = 0; x < CURSOR_WIDTH; x++) {
            cursor_background[y * CURSOR_WIDTH + x] = get_pixel(boot_info, cursor_x + x, cursor_y + y);
        }
    }

    static const uint16_t outline[CURSOR_HEIGHT] = {
        0x1000, 0x1800, 0x1C00, 0x1E00, 0x1F00, 0x1F80, 0x1FC0, 0x1FE0,
        0x1FF0, 0x1FF8, 0x1E00, 0x1700, 0x1380, 0x0180, 0x00C0, 0x0060,
        0x0030, 0x0018, 0x0000, 0x0000
    };
    static const uint16_t fill[CURSOR_HEIGHT] = {
        0x0000, 0x1000, 0x1400, 0x1600, 0x1700, 0x1780, 0x17C0, 0x17E0,
        0x17F0, 0x17E0, 0x1600, 0x1300, 0x0180, 0x0080, 0x0040, 0x0020,
        0x0000, 0x0000, 0x0000, 0x0000
    };
    for (uint32_t y = 0; y < CURSOR_HEIGHT; y++) {
        for (uint32_t x = 0; x < CURSOR_WIDTH; x++) {
            uint16_t bit = (uint16_t)(1U << (12 - x));
            if (outline[y] & bit) put_pixel(boot_info, cursor_x + x, cursor_y + y, 0x00000000);
            if (fill[y] & bit) put_pixel(boot_info, cursor_x + x, cursor_y + y, 0x00FFFFFF);
        }
    }
    cursor_visible = true;
}

void mouse_get_dirty_rect(int32_t *out_x, int32_t *out_y, uint32_t *out_w, uint32_t *out_h) {
    *out_x = dirty_x;
    *out_y = dirty_y;
    *out_w = dirty_w;
    *out_h = dirty_h;
}

MouseState* mouse_get_state(void) {
    return &g_mouse_state;
}