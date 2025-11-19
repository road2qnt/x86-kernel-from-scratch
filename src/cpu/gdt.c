#include "header/cpu/gdt.h"
#include "header/cpu/interrupt.h" // Memastikan struct TSS dan interrupt struct konsisten jika perlu

// --- Variabel Global TSS ---
struct TSS g_tss;

/**
 * global_descriptor_table, predefined GDT.
 * Initial SegmentDescriptor already set properly according to Intel Manual & OSDev.
 * Table entry : [{Null}, {Kernel Code}, {Kernel Data}, {User Code}, {User Data}, {TSS}].
 */
struct GlobalDescriptorTable global_descriptor_table = {
    .table = {
        /* [0] - Null Descriptor */
        {
            .segment_low   = 0,  .base_low      = 0,  .base_mid      = 0,
            .type_bit      = 0,  .non_system    = 0,  .privilege     = 0,
            .valid_bit     = 0,  .segment_high  = 0,  .reserved_bit  = 0,
            .long_mode     = 0,  .opr_32_bit    = 0,  .granularity   = 0,
            .base_high     = 0
        },
        /* [1] - Kernel Code (0x8) */
        {
            .base_low      = 0, .base_high     = 0,  .segment_low   = 0xFFFF,    
            .granularity   = 1, .opr_32_bit    = 1,  .privilege     = 0,         // Ring 0
            .non_system    = 1, .type_bit      = 0xA, // Executable & Readable
            .valid_bit     = 1, .long_mode     = 0,  .reserved_bit  = 0
        },
        /* [2] - Kernel Data (0x10) */
        {
            .base_low      = 0, .base_mid      = 0,  .base_high     = 0,
            .segment_low   = 0xFFFF, .granularity   = 1,  .opr_32_bit    = 1,         
            .privilege     = 0, .non_system    = 1,  .type_bit      = 0x2,       // Read/Write
            .valid_bit     = 1, .long_mode     = 0,  .reserved_bit  = 0
        },
        /* [3] - User Code (0x18) */
        {
            .base_low      = 0, .base_mid      = 0,  .base_high     = 0,
            .segment_low   = 0xFFFF, .granularity   = 1,  .opr_32_bit    = 1,         
            .privilege     = 3, // Ring 3 (User Mode)
            .non_system    = 1, .type_bit      = 0xA, // Executable & Readable
            .valid_bit     = 1, .long_mode     = 0,  .reserved_bit  = 0
        },
        /* [4] - User Data (0x20) */
        {
            .base_low      = 0, .base_mid      = 0,  .base_high     = 0,
            .segment_low   = 0xFFFF, .granularity   = 1,  .opr_32_bit    = 1,         
            .privilege     = 3, // Ring 3 (User Mode)
            .non_system    = 1, .type_bit      = 0x2, // Read/Write
            .valid_bit     = 1, .long_mode     = 0,  .reserved_bit  = 0
        },
        /* [5] - TSS Descriptor (0x28) */
        // Diinisialisasi di fungsi init_tss()
        {
            .valid_bit     = 1, // Present
            .type_bit      = 0x9, // 32-bit Available TSS
            .non_system    = 0, // System Segment
            .privilege     = 0, // Ring 0 access only
            .granularity   = 0, // Byte granularity
            // Sisa field (Base & Limit) diisi via kode
        }
    }
};

/**
 * _gdt_gdtr, predefined system GDTR. 
 */
struct GDTR _gdt_gdtr = {
    .size = sizeof(global_descriptor_table) - 1,
    .address = &global_descriptor_table
};

// --- FUNGSI TSS IMPLEMENTATION ---

void init_tss(void) {
    // 1. Bersihkan struct TSS (set 0)
    uint8_t *tss_ptr = (uint8_t*) &g_tss;
    for (uint32_t i = 0; i < sizeof(struct TSS); i++) {
        tss_ptr[i] = 0;
    }

    // 2. Set Stack Segment kernel (SS0)
    g_tss.ss0 = GDT_KERNEL_DATA_SEGMENT_SELECTOR; // 0x10

    // 3. Update entri GDT index 5 dengan base & limit dari g_tss
    uint32_t base = (uint32_t) &g_tss;
    uint32_t limit = sizeof(struct TSS) - 1;

    // Set Base Address
    global_descriptor_table.table[5].base_low  = base & 0xFFFF;
    global_descriptor_table.table[5].base_mid  = (base >> 16) & 0xFF;
    global_descriptor_table.table[5].base_high = (base >> 24) & 0xFF;

    // Set Limit
    global_descriptor_table.table[5].segment_low  = limit & 0xFFFF;
    global_descriptor_table.table[5].segment_high = (limit >> 16) & 0xF;
    
    // Pastikan flag lain benar (Type 0x9, System 0, DPL 0, P 1)
    global_descriptor_table.table[5].non_system   = 0;
    global_descriptor_table.table[5].type_bit     = 0x9;
    global_descriptor_table.table[5].privilege    = 0;
    global_descriptor_table.table[5].valid_bit    = 1;
}

void set_kernel_stack(uint32_t stack_addr) {
    // Update ESP0 di TSS. Ini dipakai CPU saat interrupt dari Ring 3 -> Ring 0
    g_tss.esp0 = stack_addr;
}

void load_tss(void) {
    // Load Task Register (LTR) dengan selector TSS (Index 5 * 8 = 0x28)
    // Pastikan GDT_TSS_SEGMENT_SELECTOR didefinisikan di gdt.h sebagai 0x28
    asm volatile("ltr %%ax" : : "a"(GDT_TSS_SEGMENT_SELECTOR));
}