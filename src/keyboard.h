#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>
#include "idt.h"

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

#define KEY_ENTER     0x0D
#define KEY_BACKSPACE 0x08
#define KEY_TAB       0x09
#define KEY_ESC       0x1B

typedef struct {
    char ascii;
    uint8_t scancode;
    bool pressed;
    bool shift;
    bool caps_lock;
} KeyEvent;

void keyboard_init(void);
__attribute__((interrupt)) void keyboard_handler(interrupt_frame_t *frame);
bool keyboard_get_char(char *out_char);

#endif
