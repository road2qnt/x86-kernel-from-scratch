#include "header/cpu/interrupt.h"
#include "header/cpu/portio.h"
#include "header/driver/framebuffer.h"
#include "header/driver/keyboard.h"
#include "header/stdlib/string.h" // Butuh ini jika mau akses memori/string

// --- TAMBAHKAN INI DI ATAS (Global Cursor State untuk Syscall) ---
static uint8_t sys_row = 0;
static uint8_t sys_col = 0;
// ----------------------------------------------------------------

void io_wait(void) {
    out(0x80, 0);
}

void pic_ack(uint8_t irq) {
    if (irq >= 8) out(PIC2_COMMAND, PIC_ACK);
    out(PIC1_COMMAND, PIC_ACK);
}

void pic_remap(void) {
    // ... (kode pic_remap tetap sama) ...
    out(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4); 
    io_wait();
    out(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    out(PIC1_DATA, PIC1_OFFSET);
    io_wait();
    out(PIC2_DATA, PIC2_OFFSET);
    io_wait();
    out(PIC1_DATA, 0b0100);
    io_wait();
    out(PIC2_DATA, 0b0010);
    io_wait();
    out(PIC1_DATA, ICW4_8086);
    io_wait();
    out(PIC2_DATA, ICW4_8086);
    io_wait();
    out(PIC1_DATA, PIC_DISABLE_ALL_MASK);
    out(PIC2_DATA, PIC_DISABLE_ALL_MASK);
}

// --- IMPLEMENTASI SYSCALL ---
void syscall_handler(struct InterruptFrame frame) {
    switch (frame.cpu.general.eax) {
        case 0: // Read (Keyboard) - Placeholder
            // Implementasi nanti
            break;
            
        case 1: // Write (Framebuffer) - Puts Char
            // EBX = Address of char, ECX = Length, EDX = Color
            {
                char c = *((char*)frame.cpu.general.ebx);
                uint8_t color = (uint8_t)frame.cpu.general.edx;

                if (c == '\n') {
                    sys_row++;
                    sys_col = 0;
                } else {
                    framebuffer_write(sys_row, sys_col, c, color, 0);
                    sys_col++;
                    if (sys_col >= 80) {
                        sys_col = 0;
                        sys_row++;
                    }
                }
                
                if (sys_row >= 25) sys_row = 0; // Reset ke atas kalau penuh (simple scroll)
                framebuffer_set_cursor(sys_row, sys_col);
            }
            break;
            
        default:
            break;
    }
}

void main_interrupt_handler(struct InterruptFrame frame) {
    switch (frame.int_number) {
        case PIC1_OFFSET + IRQ_KEYBOARD:
            keyboard_isr();
            pic_ack(IRQ_KEYBOARD);
            break;
        case PIC1_OFFSET + IRQ_TIMER:
             pic_ack(IRQ_TIMER); 
             break;
        
        case 0x80: // SYSCALL
            syscall_handler(frame);
            break;

        default:
            break;
    }
}

void activate_keyboard_interrupt(void) {
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_KEYBOARD));
}

void activate_timer_interrupt(void) {
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_TIMER));
}