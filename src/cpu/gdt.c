#include "header/cpu/gdt.h"

/**
 * global_descriptor_table, predefined GDT.
 * Initial SegmentDescriptor already set properly according to Intel Manual & OSDev.
 * Table entry : [{Null Descriptor}, {Kernel Code}, {Kernel Data (variable, etc)}, ...].
 */
struct GlobalDescriptorTable global_descriptor_table = {
    .table = {
        {
            // TODO : Implement
            // null desc
            .segment_low   = 0,
            .base_low      = 0,
            .base_mid      = 0,
            .type_bit      = 0,
            .non_system    = 0,
            .privilege     = 0,
            .valid_bit     = 0,  
            .segment_high  = 0,
            .reserved_bit  = 0,
            .long_mode     = 0,
            .opr_32_bit    = 0,
            .granularity   = 0,
            .base_high     = 0
        },
        {
            // TODO : Implement
            // kernel code
            .base_low      = 0,         
            .base_high     = 0,
            .segment_low   = 0xFFFF,    // Limit 
            .granularity   = 1,         // Scalar
            .opr_32_bit    = 1,         
            .privilege     = 0,         // Privilege = 0 (Ring 0 / Kernel)
            .non_system    = 1,         // Tipe: Segmen umum
            .type_bit      = 0xA,       // Tipe detail: 1010 (Executable & Readable)
            .valid_bit     = 1,         
            .long_mode     = 0,
            .reserved_bit  = 0
        },
        {
            // TODO : Implement
            // kernel data mode
            .base_low      = 0,         // Base = 0
            .base_mid      = 0,
            .base_high     = 0,
            .segment_low   = 0xFFFF,    
            .segment_high  = 0xF,
            .granularity   = 1,
            .opr_32_bit    = 1,         
            .privilege     = 0,         
            .non_system    = 1,
            .type_bit      = 0x2,       // Tipe detail: 0010 (Non-Executable , writable)
            .valid_bit     = 1,         
            .long_mode     = 0,
            .reserved_bit  = 0
        }
    }
};

/**
 * _gdt_gdtr, predefined system GDTR. 
 * GDT pointed by this variable is already set to point global_descriptor_table above.
 * From: https://wiki.osdev.org/Global_Descriptor_Table, GDTR.size is GDT size minus 1.
 */
struct GDTR _gdt_gdtr = {
    // TODO : Implement, this GDTR will point to global_descriptor_table. 
    //        Use sizeof operator
    .size = sizeof(global_descriptor_table) - 1,
    .address = &global_descriptor_table
};

