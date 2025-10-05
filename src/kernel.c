#include <stdint.h>
#include "header/cpu/interrupt.h"
#include "header/cpu/idt.h"
#include "header/cpu/gdt.h"
#include "header/kernel-entrypoint.h"
#include "header/stdlib/boolean.h"
#include "header/text/framebuffer.h"
#include "header/text/keyboard.h"

void kernel_setup(void) {
    // 1. Inisialisasi semua sistem dasar
    load_gdt(&_gdt_gdtr);
    pic_remap();
    initialize_idt();
    
    // 2. Aktifkan interrupt keyboard secara spesifik
    activate_keyboard_interrupt();

    // 3. Bersihkan layar dan siapkan posisi tulis
    framebuffer_clear();
    uint8_t row = 0, col = 0;

    // 4. Loop utama: cek input, tulis ke layar
    while (true) {
        char c = 0;
        get_keyboard_buffer(&c); // Ambil karakter dari buffer

        if (c != 0) {
            // Jika ada karakter, tampilkan ke layar
            framebuffer_write(row, col, c, WHITE, BLACK);
            col++; // Pindahin kursor ke kanan
            if (col >= FRAMEBUFFER_WIDTH) {
                col = 0;
                row++;
            }
            framebuffer_set_cursor(row, col);
        }
    }
}

