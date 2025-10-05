#include <stdint.h>
#include "header/cpu/interrupt.h"
#include "header/cpu/idt.h"
#include "header/cpu/gdt.h"
#include "header/kernel-entrypoint.h"
#include "header/stdlib/boolean.h"
#include "header/text/framebuffer.h"

void kernel_setup(void) {
    // 1. Muat GDT
    load_gdt(&_gdt_gdtr);
    
    // 2. Remap PIC
    pic_remap();
    
    // 3. Inisialisasi & Muat IDT
    initialize_idt();

    // 4. Tes dengan memicu interrupt nomor 3 (Breakpoint)
    __asm__("int $0x3");

    // Baris ini seharusnya tidak akan pernah dieksekusi jika debugger aktif
    framebuffer_write(0, 0, 'S', WHITE, BLACK); 

    while(true);
}

