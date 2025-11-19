#ifndef _GDT_H
#define _GDT_H

#include <stdint.h>

// --- GDT Constants ---
#define GDT_MAX_ENTRY_COUNT 32

// Segment Selectors
#define GDT_KERNEL_CODE_SEGMENT_SELECTOR 0x8
#define GDT_KERNEL_DATA_SEGMENT_SELECTOR 0x10
#define GDT_USER_CODE_SEGMENT_SELECTOR   0x18
#define GDT_USER_DATA_SEGMENT_SELECTOR   0x20
#define GDT_TSS_SEGMENT_SELECTOR         0x28

extern struct GDTR _gdt_gdtr;

/**
 * Segment Descriptor storing system segment information.
 */
struct SegmentDescriptor {
    uint16_t segment_low;   // Limit (bit 0-15)
    uint16_t base_low;      // Base (bit 0-15)
    uint8_t base_mid;       // Base (bit 16-23)
    uint8_t type_bit   : 4; // Tipe segmen
    uint8_t non_system : 1; // S bit (1 untuk Code/Data)
    uint8_t privilege  : 2; // DPL (Privilege Level 0-3)
    uint8_t valid_bit  : 1; // P bit (Present)
    uint8_t segment_high : 4; // Limit (bit 16-19)
    uint8_t reserved_bit : 1; // AVL bit (Reserved)
    uint8_t long_mode    : 1; // L bit (64-bit code)
    uint8_t opr_32_bit   : 1; // D/B bit (32-bit mode)
    uint8_t granularity  : 1; // G bit (Granularity)
    uint8_t base_high;      // Base (bit 24-31)
} __attribute__((packed));

/**
 * Global Descriptor Table containing list of segment descriptor.
 */
struct GlobalDescriptorTable {
    struct SegmentDescriptor table[GDT_MAX_ENTRY_COUNT];
} __attribute__((packed));

/**
 * GDTR, carrying information where's the GDT located and GDT size.
 */
struct GDTR {
    uint16_t                     size;
    struct GlobalDescriptorTable *address;
} __attribute__((packed));

// --- DEFINISI STRUCT TSS (INI YANG HILANG TADI) ---
struct TSS {
    uint32_t prev_tss; // Previous TSS
    uint32_t esp0;     // Stack Pointer Ring 0 (Kernel Stack)
    uint32_t ss0;      // Stack Segment Ring 0
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

// Deklarasi Global TSS
extern struct TSS g_tss;

// --- Function Prototypes ---

// Load GDT (Assembly function)
extern void load_gdt(struct GDTR *gdtr);

// TSS Functions (Implemented in gdt.c)
void init_tss(void);
void set_kernel_stack(uint32_t stack_addr);
void load_tss(void);

#endif