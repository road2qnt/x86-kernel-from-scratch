#include "header/cpu/gdt.h"
#include "header/cpu/interrupt.h"
#include "header/cpu/idt.h"
#include "header/driver/framebuffer.h"
#include "header/driver/keyboard.h"
#include "header/filesystem/ext2.h"
#include "header/memory/paging.h" // <--- JANGAN LUPA INI

void kernel_setup(void) {
    // 1. Init Dasar (GDT, IDT, PIC, dll)
    load_gdt(&_gdt_gdtr);
    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);

    // 2. Init Filesystem
    initialize_filesystem_ext2();
    if (g_filesystem_initialized) {
        framebuffer_write(0, 0, 'F', GREEN, BLACK); // F = Filesystem OK
    } else {
        framebuffer_write(0, 0, 'X', RED, BLACK);
    }

    // --- 3. AKTIVASI PAGING (VIRTUAL MEMORY) ---
    
    // A. Load Page Directory ke register CR3
    // Kita kasih alamat fisik _paging_kernel_page_directory ke CPU
    asm volatile(
        "mov %0, %%cr3"
        : /* Output */
        : "r"(&_paging_kernel_page_directory) /* Input */
        : "memory"
    );

    // B. Aktifkan PSE (Page Size Extension) di CR4 (Bit ke-4)
    // Ini wajib karena kita pake halaman 4MB, bukan 4KB standar
    uint32_t cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= 0x00000010; // Set bit ke-4
    asm volatile("mov %0, %%cr4" : : "r"(cr4));

    // C. Aktifkan Paging di CR0 (Bit ke-31)
    // INI TITIK KRITIS: Kalau crash, biasanya di sini.
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000; // Set bit ke-31 (PG - Paging)
    asm volatile("mov %0, %%cr0" : : "r"(cr0));
    
    // --------------------------------------------

    // Kalau sampai sini OS gak restart, berarti Paging SUKSES!
    framebuffer_write(0, 1, 'P', GREEN, BLACK); // P = Paging OK

    while(true);
}