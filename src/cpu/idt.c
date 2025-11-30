#include "header/cpu/interrupt.h"
#include "header/cpu/idt.h" 
#include "header/cpu/gdt.h"

// Mendefinisikan variabel global yang "dijanjikan" di .h
struct InterruptDescriptorTable interrupt_descriptor_table;
struct IDTR _idt_idtr;

extern void *isr_stub_table[];

//isi alamat
void set_interrupt_gate(uint8_t int_vector, void *handler_address, uint16_t gdt_seg_selector, uint8_t privilege) {
    struct IDTGate *idt_gate = &interrupt_descriptor_table.table[int_vector];

    idt_gate->offset_low        = ((uint32_t) handler_address) & 0xFFFF;
    idt_gate->offset_high       = ((uint32_t) handler_address) >> 16;

    idt_gate->segment_selector = gdt_seg_selector;
    idt_gate->privilege        = privilege;
    
    idt_gate->present   = 1;      // Mengganti valid_bit
    idt_gate->gate_type = 0b01110;  // Tipe 32-bit Interrupt Gate
    
    idt_gate->_reserved = 0;
}


void initialize_idt(void) {
    _idt_idtr.address = &interrupt_descriptor_table;
    _idt_idtr.size    = sizeof(interrupt_descriptor_table) - 1;

    for (uint16_t i = 0; i < ISR_STUB_TABLE_LIMIT; i++) {
        set_interrupt_gate(i, isr_stub_table[i], GDT_KERNEL_CODE_SEGMENT_SELECTOR, 0);
    }
    set_interrupt_gate(0x80, isr_stub_table[0x80], GDT_KERNEL_CODE_SEGMENT_SELECTOR, 3);
    interrupt_descriptor_table.table[0x80].gate_type = 0b01111; // 32-bit Trap Gate for syscall
    
    __asm__ volatile("lidt %0" : : "m"(_idt_idtr));
    __asm__ volatile("sti");
}