#include <stdint.h>

// Definisi manual biar gak perlu include header lain yang mungkin bermasalah
#define FRAMEBUFFER_MEMORY_OFFSET ((volatile uint16_t*) 0xC00B8000) // Virtual Address VGA

void framebuffer_write_char(int row, int col, char c) {
    volatile uint16_t *video_memory = FRAMEBUFFER_MEMORY_OFFSET;
    uint16_t position = row * 80 + col;
    video_memory[position] = (c | (0x0F << 8)); // Putih di Hitam
}

void kernel_setup(void) {
    // JANGAN LOAD GDT
    // JANGAN LOAD IDT
    // JANGAN INIT FS
    
    // Cukup tulis satu huruf
    framebuffer_write_char(0, 0, 'X');
    framebuffer_write_char(0, 1, 'D');

    // Loop diam
    while(1);
}