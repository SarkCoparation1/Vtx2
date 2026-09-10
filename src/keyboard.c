#include "keyboard.h"
#include "irqctl.h"

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static bool shift_pressed = false;
static bool caps_lock_active = false;

#define BUFFER_SIZE 256
static char kbd_buffer[BUFFER_SIZE];
static uint16_t buffer_head = 0;
static uint16_t buffer_tail = 0;

static const char scancode_to_tr_q[] = {
    0,   27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '*', '-', '\b',
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', 'g', 'u', '\n',
    0,  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', 's', 'i', '"',
    0,  '<', 'z', 'x', 'c', 'v', 'b', 'n', 'm', 'o', 'c', '.',
    0,  '*',   0, ' '
};

static const char scancode_to_tr_q_shift[] = {
    0,   27, '!', '\'', '^', '+', '%', '&', '/', '(', ')', '=', '?', '_', '\b',
  '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', 'G', 'U', '\n',
    0,  'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 'S', 'I', 'e',
    0,  '>', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', 'O', 'C', ':',
    0,  '*',   0, ' '
};

static void buffer_push(char c) {
    uint16_t next = (buffer_head + 1) % BUFFER_SIZE;
    if (next != buffer_tail) {
        kbd_buffer[buffer_head] = c;
        buffer_head = next;
    }
}

void keyboard_init(void) {
    buffer_head = 0;
    buffer_tail = 0;
    shift_pressed = false;
    caps_lock_active = false;
}

__attribute__((interrupt)) void keyboard_handler(interrupt_frame_t *frame) {
    (void)frame;
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

    if (scancode & 0x80) {
        uint8_t released_code = scancode & 0x7F;
        if (released_code == 0x2A || released_code == 0x36) {
            shift_pressed = false;
        }
    } else { // Tuşa Basıldı
        if (scancode == 0x2A || scancode == 0x36) {
            shift_pressed = true;
        } else if (scancode == 0x3A) {
            caps_lock_active = !caps_lock_active;
        } else if (scancode < sizeof(scancode_to_tr_q)) {
            bool use_uppercase = shift_pressed ^ caps_lock_active;
            char ch = use_uppercase ? scancode_to_tr_q_shift[scancode] : scancode_to_tr_q[scancode];

            if (ch != 0) {
                buffer_push(ch);
            }
        }
    }

    irqctl_eoi(1);
}

bool keyboard_get_char(char *out_char) {
    if (buffer_head == buffer_tail) {
        return false;
    }

    *out_char = kbd_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % BUFFER_SIZE;
    return true;
}