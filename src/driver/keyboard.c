#include "header/driver/keyboard.h"
#include "header/cpu/portio.h"
#include "header/stdlib/string.h"
#include "header/cpu/interrupt.h"

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
    .keyboard_buffer    = 0,
};
void keyboard_state_activate(void) {
    keyboard_state.keyboard_input_on = true;
}


void keyboard_state_deactivate(void) {
    keyboard_state.keyboard_input_on = false;
}

void get_keyboard_buffer(char *buf) {
    *buf = keyboard_state.keyboard_buffer;
    keyboard_state.keyboard_buffer = 0; 
}

void keyboard_isr(void) {
    // Jika driver sedang tidak aktif, abaikan interrupt dan langsung keluar.
    if (!keyboard_state.keyboard_input_on) {
        // Tetap baca port-nya sekali biar hardware-nya 'senang'
        in(KEYBOARD_DATA_PORT);
        return;
    }

    // Baca scancode dari hardware
    uint8_t scancode = in(KEYBOARD_DATA_PORT);

    // Cek apakah ini adalah scancode 'extended' (misal: arrow keys)
    if (scancode == EXTENDED_SCANCODE_BYTE) {
        keyboard_state.read_extended_mode = true;
        return; // Keluar, tunggu byte berikutnya
    }

    // Jika mode extended aktif, proses scancode khusus
    if (keyboard_state.read_extended_mode) {
        keyboard_state.read_extended_mode = false; // Matikan lagi mode-nya
        switch (scancode) {
                case EXT_SCANCODE_UP:
                    keyboard_state.keyboard_buffer = KEY_ARROW_UP;
                    break;
                case EXT_SCANCODE_DOWN:
                    keyboard_state.keyboard_buffer = KEY_ARROW_DOWN;
                    break;
                case EXT_SCANCODE_LEFT:
                    keyboard_state.keyboard_buffer = KEY_ARROW_LEFT;
                    break;
                case EXT_SCANCODE_RIGHT:
                    keyboard_state.keyboard_buffer = KEY_ARROW_RIGHT;
                    break;
                case EXT_SCANCODE_DELETE: 
                    keyboard_state.keyboard_buffer = KEY_DELETE;
                    break;
        }
        return;
    }

    // Proses scancode normal (bukan extended)
    // Abaikan saat tombol dilepas (break code, biasanya >= 0x80)
    if (scancode < 0x80) {
        // Terjemahkan scancode ke ASCII menggunakan kamus
        char c = keyboard_scancode_1_to_ascii_map[scancode];
        // Simpan ke buffer jika karakternya valid
        if (c != 0) {
            keyboard_state.keyboard_buffer = c;
        }
    }
}
