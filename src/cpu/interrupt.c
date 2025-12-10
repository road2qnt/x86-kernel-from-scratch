#include "header/cpu/interrupt.h"
#include "header/cpu/portio.h"
#include "header/cpu/gdt.h"
#include "header/driver/framebuffer.h"
#include "header/driver/keyboard.h"
#include "header/stdlib/string.h"
#include "header/filesystem/ext2.h"

static void syscall_handler(struct CPURegister *regs);

// --- TAMBAHKAN INI DI ATAS (Global Cursor State untuk Syscall) ---
static uint8_t sys_row = 0;
static uint8_t sys_col = 0;

// External ISR stub table
extern void *isr_stub_table[];

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

void main_interrupt_handler(struct CPURegister *regs, uint32_t int_number) {
    switch (int_number) {
        case PIC1_OFFSET + IRQ_KEYBOARD:
            keyboard_isr();
            pic_ack(IRQ_KEYBOARD);
            break;
        case PIC1_OFFSET + IRQ_TIMER:
             pic_ack(IRQ_TIMER); 
             break;
        
        case 0x80: // SYSCALL
            syscall_handler(regs);
            break;

        default:
            break;
    }
}

// --- IMPLEMENTASI SYSCALL ---
static void syscall_handler(struct CPURegister *regs) {
    // EAX menyimpan nomor layanan syscall
    // EBX, ECX, EDX menyimpan parameter
    switch (regs->general.eax) {
        case SYSCALL_READ:
            // read(struct EXT2DriverRequest request)
            // Parameter ada di EBX (pointer ke request)
            // Return value ditaruh di ECX (pointer ke retcode) - Sesuai tabel buku
            *((int8_t*)regs->general.ecx) = read(
                *(struct EXT2DriverRequest*)regs->general.ebx
            );
            break;
            
        case SYSCALL_READ_DIR:
            *((int8_t*)regs->general.ecx) = read_directory(
                (struct EXT2DriverRequest*)regs->general.ebx
            );
            break;

        case SYSCALL_WRITE:
            *((int8_t*)regs->general.ecx) = write(
                (struct EXT2DriverRequest*)regs->general.ebx
            );
            break;

        case SYSCALL_DELETE:
            *((int8_t*)regs->general.ecx) = delete(
                *(struct EXT2DriverRequest*)regs->general.ebx
            );
            break;

        case SYSCALL_GETCHAR:
            // get_keyboard_buffer(char *buf)
            // EBX = pointer char buffer
            get_keyboard_buffer((char*)regs->general.ebx);
            break;

        case SYSCALL_PUTCHAR:
            // putchar(char c, uint8_t color)
            // EBX = char, ECX = color
            {
                char c = (char)regs->general.ebx;
                uint8_t color = (uint8_t)regs->general.ecx;
                
                if (c == '\b') {
                    // Backspace: move cursor back and clear character
                    if (sys_col > 0) {
                        sys_col--;
                    } else if (sys_row > 0) {
                        sys_row--;
                        sys_col = 79;
                    }
                    framebuffer_write(sys_row, sys_col, ' ', color, 0);
                } else if (c == '\n') {
                    sys_col = 0;
                    sys_row++;
                } else {
                    framebuffer_write(sys_row, sys_col, c, color, 0);
                    sys_col++;
                    if (sys_col >= 80) { sys_col = 0; sys_row++; }
                }
                
                // Scroll if needed
                if (sys_row >= 25) {
                    // Scroll up: move all lines up by 1
                    for (uint8_t row = 0; row < 24; row++) {
                        for (uint8_t col = 0; col < 80; col++) {
                            uint16_t src_idx = (row + 1) * 80 + col;
                            uint16_t dst_idx = row * 80 + col;
                            volatile uint16_t *fb = (volatile uint16_t *)0xC00B8000;
                            fb[dst_idx] = fb[src_idx];
                        }
                    }
                    // Clear last line
                    for (uint8_t col = 0; col < 80; col++) {
                        framebuffer_write(24, col, ' ', 0x07, 0);
                    }
                    sys_row = 24;
                }
                framebuffer_set_cursor(sys_row, sys_col);
            }
            break;

        case SYSCALL_PUTS:
            // puts(char *str, uint32_t len, uint8_t color)
            // EBX = str, ECX = len, EDX = color
            {
                char *str = (char*)regs->general.ebx;
                uint32_t len = regs->general.ecx;
                uint8_t color = (uint8_t)regs->general.edx;
                for (uint32_t i = 0; i < len; i++) {
                    char c = str[i];
                    if (c == '\b') {
                        // Backspace
                        if (sys_col > 0) {
                            sys_col--;
                        } else if (sys_row > 0) {
                            sys_row--;
                            sys_col = 79;
                        }
                        framebuffer_write(sys_row, sys_col, ' ', color, 0);
                    } else if (c == '\n') {
                        sys_col = 0;
                        sys_row++;
                    } else {
                        framebuffer_write(sys_row, sys_col, c, color, 0);
                        sys_col++;
                        if (sys_col >= 80) { sys_col = 0; sys_row++; }
                    }
                    
                    // Scroll if needed
                    if (sys_row >= 25) {
                        for (uint8_t row = 0; row < 24; row++) {
                            for (uint8_t col = 0; col < 80; col++) {
                                uint16_t src_idx = (row + 1) * 80 + col;
                                uint16_t dst_idx = row * 80 + col;
                                volatile uint16_t *fb = (volatile uint16_t *)0xC00B8000;
                                fb[dst_idx] = fb[src_idx];
                            }
                        }
                        for (uint8_t col = 0; col < 80; col++) {
                            framebuffer_write(24, col, ' ', 0x07, 0);
                        }
                        sys_row = 24;
                    }
                }
                framebuffer_set_cursor(sys_row, sys_col);
            }
            break;

        case SYSCALL_ACTIVATE_KBD:
            keyboard_state_activate();
            break;

        case SYSCALL_STAT:
            // stat(name, name_len, parent_inode, size_out, is_dir_out)
            // EBX = pointer to struct {name, name_len, parent_inode}
            // ECX = pointer to size_out (uint32_t*)
            // EDX = pointer to is_dir_out (bool*)
            // Return: EAX = return code
            {
                struct __attribute__((packed)) {
                    char *name;
                    uint8_t name_len;
                    uint32_t parent_inode;
                } *stat_req = (void*)regs->general.ebx;
                
                regs->general.eax = (uint32_t)stat(
                    stat_req->name,
                    stat_req->name_len,
                    stat_req->parent_inode,
                    (uint32_t*)regs->general.ecx,
                    (bool*)regs->general.edx
                );
            }
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

struct TSSEntry _interrupt_tss_entry = {
    .ss0 = GDT_KERNEL_DATA_SEGMENT_SELECTOR,
};

void set_tss_kernel_current_stack(void) {
    uint32_t stack_ptr;
    // Reading base stack frame instead esp
    __asm__ volatile ("mov %%ebp, %0": "=r"(stack_ptr) : /* <Empty> */);
    // Add 8 because 4 for ret address and other 4 is for stack_ptr variable
    _interrupt_tss_entry.esp0 = stack_ptr + 8;
}
   