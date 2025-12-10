#include "header/driver/keyboard.h"
#include "header/cpu/portio.h"
#include "header/stdlib/string.h"
#include "header/cpu/interrupt.h"
#include "header/driver/framebuffer.h"

const char keyboard_scancode_1_to_ascii_map[256] = {
      0, 0x1B, '1', '2', '3', '4', '5', '6',  '7', '8', '9',  '0',  '-', '=', '\b', '\t',
    'q',  'w', 'e', 'r', 't', 'y', 'u', 'i',  'o', 'p', '[',  ']', '\n',   0,  'a',  's',
    'd',  'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0, '\\',  'z', 'x',  'c',  'v',
    'b',  'n', 'm', ',', '.', '/',   0, '*',    0, ' ',   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0, '-',    0,    0,   0,  '+',    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,

      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
};

static struct KeyboardDriverState keyboard_state = {
    .read_extended_mode = false,
    .keyboard_input_on  = false,
    .head = 0,
    .tail = 0,
};

void keyboard_state_activate(void) {
    keyboard_state.keyboard_input_on = true;
}


void keyboard_state_deactivate(void) {
    keyboard_state.keyboard_input_on = false;
}

void get_keyboard_buffer(char *buf) {
    // If buffer is empty, return 0
    if (keyboard_state.head == keyboard_state.tail) {
        *buf = 0;
        return;
    }

    // Read character from tail
    *buf = keyboard_state.buffer[keyboard_state.tail];
    // Move tail to the next position
    keyboard_state.tail = (keyboard_state.tail + 1) % KEYBOARD_BUFFER_SIZE;
}

static void add_to_buffer(char c) {
    // Calculate next head position
    uint32_t next_head = (keyboard_state.head + 1) % KEYBOARD_BUFFER_SIZE;

    // Check if buffer is full
    if (next_head == keyboard_state.tail) {
        // Buffer is full, character is dropped
        return;
    }

    // Add character to buffer and update head
    keyboard_state.buffer[keyboard_state.head] = c;
    keyboard_state.head = next_head;
}

void keyboard_isr(void) {
    if (!keyboard_state.keyboard_input_on) {
        in(KEYBOARD_DATA_PORT);
        return;
    }

    uint8_t scancode = in(KEYBOARD_DATA_PORT);

    if (scancode == EXTENDED_SCANCODE_BYTE) {
        keyboard_state.read_extended_mode = true;
        return;
    }

    if (keyboard_state.read_extended_mode) {
        keyboard_state.read_extended_mode = false;
        switch (scancode) {
            case EXT_SCANCODE_UP:
                add_to_buffer(KEY_ARROW_UP);
                break;
            case EXT_SCANCODE_DOWN:
                add_to_buffer(KEY_ARROW_DOWN);
                break;
            case EXT_SCANCODE_LEFT:
                add_to_buffer(KEY_ARROW_LEFT);
                break;
            case EXT_SCANCODE_RIGHT:
                add_to_buffer(KEY_ARROW_RIGHT);
                break;
            case EXT_SCANCODE_DELETE: 
                add_to_buffer(KEY_DELETE);
                break;
        }
        return;
    }

    if (scancode < 0x80) { // Key press
        char c = keyboard_scancode_1_to_ascii_map[scancode];
        if (c != 0) {
            add_to_buffer(c);
        }
    }
}
