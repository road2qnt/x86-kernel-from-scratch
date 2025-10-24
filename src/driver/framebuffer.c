#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/driver/framebuffer.h"
#include "header/stdlib/string.h"
#include "header/cpu/portio.h"

void framebuffer_set_cursor(uint8_t r, uint8_t c) {
    // TODO : Implement
    uint16_t position = r * FRAMEBUFFER_WIDTH + c;
    out(FRAMEBUFFER_COMMAND_PORT, 0x0E);
    out(FRAMEBUFFER_DATA_PORT, (uint8_t) ((position >> 8) & 0xFF));
    out(FRAMEBUFFER_COMMAND_PORT, 0x0F);
    out(FRAMEBUFFER_DATA_PORT, (uint8_t) (position & 0xFF));
}

void framebuffer_write(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg) {
    // TODO : Implement
    volatile uint16_t *video_memory = (uint16_t*) FRAMEBUFFER_MEMORY_OFFSET;
    uint16_t position = row * FRAMEBUFFER_WIDTH + col;
    uint8_t attribute = (bg << 4) | (fg & 0x0F);
    video_memory[position] = (c | (attribute << 8));
}

void framebuffer_clear(void) {
    // TODO : Implement
    uint16_t clear_char = ' ' | ( (BLACK << 4) | (WHITE & 0x0F) ) << 8;

    memset16((uint16_t*) FRAMEBUFFER_MEMORY_OFFSET, clear_char, FRAMEBUFFER_WIDTH * FRAMEBUFFER_HEIGHT);
    
    framebuffer_set_cursor(0, 0);
}
