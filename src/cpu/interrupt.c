#include "header/cpu/interrupt.h"
#include "header/cpu/portio.h"
#include "header/driver/framebuffer.h"
#include "header/driver/keyboard.h"


void io_wait(void) {
    out(0x80, 0);
}

void pic_ack(uint8_t irq) {
    if (irq >= 8) out(PIC2_COMMAND, PIC_ACK);
    out(PIC1_COMMAND, PIC_ACK);
}

void pic_remap(void) {
    // Starts the initialization sequence in cascade mode
    out(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4); 
    io_wait();
    out(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    out(PIC1_DATA, PIC1_OFFSET); // ICW2: Master PIC vector offset
    io_wait();
    out(PIC2_DATA, PIC2_OFFSET); // ICW2: Slave PIC vector offset
    io_wait();
    out(PIC1_DATA, 0b0100); // ICW3: tell Master PIC, slave PIC at IRQ2 (0000 0100)
    io_wait();
    out(PIC2_DATA, 0b0010); // ICW3: tell Slave PIC its cascade identity (0000 0010)
    io_wait();

    out(PIC1_DATA, ICW4_8086);
    io_wait();
    out(PIC2_DATA, ICW4_8086);
    io_wait();

    // Disable all interrupts
    out(PIC1_DATA, PIC_DISABLE_ALL_MASK);
    out(PIC2_DATA, PIC_DISABLE_ALL_MASK);
}
void main_interrupt_handler(struct InterruptFrame frame) {
    switch (frame.int_number) {
        // i am so sleepy finally implemented : )
        case PIC1_OFFSET + IRQ_KEYBOARD: // 0x20 + 1 = 0x21 (Interrupt 33)
            keyboard_isr();
            pic_ack(IRQ_KEYBOARD); // Lapor ke PIC "udah beres"
            break;
        case PIC1_OFFSET + IRQ_TIMER:
             pic_ack(IRQ_TIMER); 
             break;
        
        default:
            // Biarin default ini buat nangkep interrupt lain
            break;
    }
}
void activate_keyboard_interrupt(void) {
    // mask n unmask
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_KEYBOARD));

}

void activate_timer_interrupt(void) {
    // Unmask IRQ0 (Timer) pada Master PIC
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_TIMER));
}
