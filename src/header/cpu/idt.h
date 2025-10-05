#ifndef _IDT_H
#define _IDT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Konstanta
#define IDT_MAX_ENTRY_COUNT    256
#define ISR_STUB_TABLE_LIMIT   256 // Disesuaikan biar sama
#define INTERRUPT_GATE_R_BIT_1 0b000
#define INTERRUPT_GATE_R_BIT_2 0b1110 // Ini tipe Interrupt Gate 32-bit
#define INTERRUPT_GATE_R_BIT_3 0b0

// "Janji" bahwa ada tabel ISR stubs dan IDTR di file lain
extern void *isr_stub_table[ISR_STUB_TABLE_LIMIT];
extern struct IDTR _idt_idtr;

/**
 * IDTGate: Blueprint untuk satu entri/halaman di IDT.
 * Strukturnya harus presisi 8 byte sesuai manual Intel.
 */
struct IDTGate {
    uint16_t offset_low;        // 16 bit bawah dari alamat handler
    uint16_t segment_selector;  // Selector GDT untuk kode kernel (0x8)
    uint8_t  _reserved;         // Harus nol
    uint8_t  gate_type : 5;     // Tipe gate (misal: 0b01110 untuk 32-bit Interrupt Gate)
    uint8_t  privilege : 2;     // Level keamanan (0-3)
    uint8_t  present : 1;       // 1 = entri ini valid/aktif
    uint16_t offset_high;       // 16 bit atas dari alamat handler
} __attribute__((packed));

/**
 * Interrupt Descriptor Table: Blueprint untuk "Buku Prosedur Darurat" lengkap.
 * Isinya adalah array dari 256 IDTGate.
 */
struct InterruptDescriptorTable {
    struct IDTGate table[IDT_MAX_ENTRY_COUNT];
} __attribute__((packed));

/**
 * IDTR: Blueprint untuk "Penanda Buku" yang akan kita kasih ke CPU.
 * Isinya alamat dan ukuran dari IDT.
 */
struct IDTR {
    uint16_t                     size;
    struct InterruptDescriptorTable *address;
} __attribute__((packed));

// Deklarasi fungsi-fungsi yang akan kita implementasikan di idt.c
void set_interrupt_gate(uint8_t int_vector, void *handler_address, uint16_t gdt_seg_selector, uint8_t privilege);
void initialize_idt(void);

#endif