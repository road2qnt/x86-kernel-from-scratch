#include "header/cpu/interrupt.h"
#include "header/cpu/portio.h"
#include "header/driver/framebuffer.h"
#include "header/driver/keyboard.h"
#include "header/stdlib/string.h"
#include "header/filesystem/ext2.h"

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
    // EAX menyimpan nomor layanan syscall
    // EBX, ECX, EDX menyimpan parameter
    switch (frame.cpu.general.eax) {
        case SYSCALL_READ:
            // read(struct EXT2DriverRequest request)
            // Parameter ada di EBX (pointer ke request)
            // Return value ditaruh di ECX (pointer ke retcode) - Sesuai tabel buku
            *((int8_t*)frame.cpu.general.ecx) = read(
                *(struct EXT2DriverRequest*)frame.cpu.general.ebx
            );
            break;
            
        case SYSCALL_READ_DIR:
            *((int8_t*)frame.cpu.general.ecx) = read_directory(
                (struct EXT2DriverRequest*)frame.cpu.general.ebx
            );
            break;

        case SYSCALL_WRITE:
            *((int8_t*)frame.cpu.general.ecx) = write(
                (struct EXT2DriverRequest*)frame.cpu.general.ebx
            );
            break;

        case SYSCALL_DELETE:
            *((int8_t*)frame.cpu.general.ecx) = delete(
                *(struct EXT2DriverRequest*)frame.cpu.general.ebx
            );
            break;

        case SYSCALL_GETCHAR:
            // get_keyboard_buffer(char *buf)
            // EBX = pointer char buffer
            get_keyboard_buffer((char*)frame.cpu.general.ebx);
            break;

        case SYSCALL_PUTCHAR:
            // putchar(char c, uint8_t color)
            // EBX = char, ECX = color
            framebuffer_write(sys_row, sys_col, (char)frame.cpu.general.ebx, (uint8_t)frame.cpu.general.ecx, 0);
            sys_col++;
            if (sys_col >= 80) { sys_col = 0; sys_row++; }
            if (sys_row >= 25) sys_row = 0;
            framebuffer_set_cursor(sys_row, sys_col);
            break;

        case SYSCALL_PUTS:
            // puts(char *str, uint32_t len, uint8_t color)
            // EBX = str, ECX = len, EDX = color
            {
                char *str = (char*)frame.cpu.general.ebx;
                uint32_t len = frame.cpu.general.ecx;
                uint8_t color = (uint8_t)frame.cpu.general.edx;
                for (uint32_t i = 0; i < len; i++) {
                    // Handle newline
                    if (str[i] == '\n') {
                        sys_col = 0;
                        sys_row++;
                    } else {
                        framebuffer_write(sys_row, sys_col, str[i], color, 0);
                        sys_col++;
                        if (sys_col >= 80) { sys_col = 0; sys_row++; }
                    }
                    if (sys_row >= 25) sys_row = 0; 
                }
                framebuffer_set_cursor(sys_row, sys_col);
            }
            break;

        case SYSCALL_ACTIVATE_KBD:
            keyboard_state_activate();
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