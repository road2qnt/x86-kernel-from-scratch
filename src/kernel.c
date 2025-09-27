#include <stdint.h>
#include "header/cpu/gdt.h"
#include "header/kernel-entrypoint.h"
#include "header/stdlib/boolean.h"
#include "header/text/framebuffer.h"

void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);
    framebuffer_clear();
    framebuffer_write(5, 10, 'A', WHITE, BLACK); 
    framebuffer_set_cursor(5, 11); // Pindahin kursor ke sebelah huruf 'A'
    while (true);
}

