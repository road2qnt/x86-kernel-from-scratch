#include "header/cpu/gdt.h"
#include "header/cpu/interrupt.h"
#include "header/cpu/idt.h"
#include "header/driver/framebuffer.h"
#include "header/driver/keyboard.h"
#include "header/filesystem/ext2.h"
#include "header/memory/paging.h"
#include "header/stdlib/string.h"

// Deklarasi Global
extern void enter_user_mode();
extern struct PageDirectory _paging_kernel_page_directory; 

// Stack untuk TSS (Kernel Stack saat interrupt dari User Mode)
#define KERNEL_STACK_SIZE 8192 
char kernel_stack[KERNEL_STACK_SIZE]; 

void kernel_setup(void) {
    // ===============================================================
    // FASE 1: SETUP INFRASTRUKTUR UTAMA (GDT/IDT/TSS)
    // ===============================================================
    
    // 1. Load GDT (Pake alamat virtual aman karena Paging udah ON dari Assembly)
    load_gdt(&_gdt_gdtr);
    
    // 2. Reload Segment Registers (Wajib untuk sinkronisasi GDT baru)
    // Kita gunakan wrapper assembly untuk memasukkan selector data (0x10)
    asm volatile(
        "mov %0, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        : : "i"(GDT_KERNEL_DATA_SEGMENT_SELECTOR) : "ax", "memory"
    );

    // 3. Setup TSS (Biar bisa balik dari User Mode nanti)
    init_tss();
    // Set stack kernel ke puncak array kernel_stack
    set_kernel_stack((uint32_t)kernel_stack + KERNEL_STACK_SIZE);
    load_tss();

    // 4. Setup Interrupts
    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();
    activate_timer_interrupt(); // Penting biar gak crash loop karena IRQ0
    
    // Aktifkan Interrupt Global
    asm volatile("sti"); 

    // ===============================================================
    // FASE 2: DRIVERS & FILESYSTEM
    // ===============================================================
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);

    initialize_filesystem_ext2();
    if (g_filesystem_initialized) {
        framebuffer_write(0, 0, 'F', GREEN, BLACK); // F = Filesystem OK
    } else {
        framebuffer_write(0, 0, 'X', RED, BLACK);
    }

    // ===============================================================
    // FASE 3: SWITCH PAGING (Assembly -> C)
    // ===============================================================
    // Saat ini kita jalan pakai Page Table "sementara" dari Assembly.
    // Kita harus switch ke Page Directory "resmi" (_paging_kernel_page_directory)
    // supaya fungsi alokasi memori (paging_allocate) bekerja pada tabel yang aktif.
    
    // Hitung Alamat Fisik dari Page Directory C
    // (Virtual Address - Offset 3GB)
    uint32_t pd_physical = (uint32_t)&_paging_kernel_page_directory - 0xC0000000;
    
    // Load ke CR3
    asm volatile(
        "mov %0, %%cr3"
        : /* No Output */
        : "r"(pd_physical) /* Input */
        : "memory"
    );
    
    framebuffer_write(0, 1, 'P', GREEN, BLACK); // P = Paging Switched

    // ===============================================================
    // FASE 4: PREPARE USER SPACE & LOADER
    // ===============================================================
    framebuffer_write(0, 2, 'R', GREEN, BLACK); // R = Ready

    // 1. Alokasi Memori untuk STACK User (di 0x800000 - 8MB)
    // Kita pake 8MB biar aman dari identity map 0-4MB
    void *user_stack_vaddr = (void*)0x800000; 
    if (paging_allocate_user_page_frame(&_paging_kernel_page_directory, user_stack_vaddr)) {
        framebuffer_write(0, 3, 'S', GREEN, BLACK); // S = Stack Mapped
    } else {
        framebuffer_write(0, 3, 'E', RED, BLACK); // E = Error Alloc Stack
        while(true);
    }

    // 2. Alokasi Memori untuk KODE User (di 0x400000 - 4MB)
    // INI PENTING: Kita harus kasih 'wadah' memori fisik buat naruh kode shell
    if (paging_allocate_user_page_frame(&_paging_kernel_page_directory, (void*)0x400000)) {
        framebuffer_write(0, 4, 'M', GREEN, BLACK); // M = Memory Code Alloc OK
    } else {
        framebuffer_write(0, 4, 'E', RED, BLACK); // Error Alloc Code
        while(true);
    }

    // 3. LOAD PROGRAM "shell" DARI DISK KE RAM 0x400000
    struct EXT2DriverRequest req;
    memset(&req, 0, sizeof(req));
    req.parent_inode = ROOT_INODE_NO; // File ada di root
    req.name = "shell";               // Nama file yang di-insert
    req.name_len = 5;
    req.buf = (void*)0x400000;        // <<< TARGET LOAD ADDRESS (Virtual)
    req.buffer_size = 0x100000;       // 1MB buffer (shell bisa besar karena banyak command)
    req.is_directory = false;         // Wajib set ini
    
    int8_t ret = read(req);           // Baca file
    
    if (ret == 0) {
        framebuffer_write(0, 5, 'L', GREEN, BLACK); // L = Load OK
    } else {
        framebuffer_write(0, 5, 'F', RED, BLACK); // F = File Not Found / Error
        // Kalau gagal load, jangan lompat, nanti crash
        while(true); 
    }

    // ===============================================================
    // FASE 5: LOMPAT (JUMP) KE USER MODE
    // ===============================================================
    // Sekarang di 0x400000 sudah ada kode shell beneran. GAS!
    enter_user_mode();

    while(true);
}