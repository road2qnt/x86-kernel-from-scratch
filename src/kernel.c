#include "header/cpu/gdt.h"
#include "header/cpu/interrupt.h"
#include "header/cpu/idt.h"
#include "header/kernel-entrypoint.h"
#include "header/driver/framebuffer.h"
#include "header/driver/keyboard.h"
#include "header/driver/disk.h" // <-- Include disk.h

// Definisikan struct buffer sederhana untuk nampung 1 blok data (512 byte)
struct BlockBuffer {
    uint8_t buf[512];
};

void kernel_setup(void) {
    // Inisialisasi sistem dasar
    load_gdt(&_gdt_gdtr);
    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();
    keyboard_state_activate();

    framebuffer_clear();

    // --- TES TULIS KE DISK ---
    framebuffer_write(0, 0, 'W', WHITE, BLACK); // Tanda mulai nulis

    // 1. Siapin data untuk ditulis (misal: 0, 1, 2, ..., 15 berulang)
    struct BlockBuffer b;
    for (int i = 0; i < 512; i++) {
        b.buf[i] = i % 16;
    }

    // 2. Tulis 1 blok data (isi variabel b) ke LBA 17
    write_blocks(&b, 17, 1);

    framebuffer_write(0, 1, 'D', WHITE, BLACK); // Tanda selesai nulis

    // -------------------------

    while(true); // Hentikan CPU di sini
}