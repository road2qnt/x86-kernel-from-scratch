#include <stdint.h>

// Wrapper Syscall Manual (karena gak ada stdlib)
void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("int $0x80" : : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx));
}

void print_user(char *str) {
    // Cari panjang string manual
    int len = 0;
    while (str[len]) len++;
    
    // Syscall Write (EAX=1, EBX=str, ECX=len, EDX=Warna)
    // Warna 0xF = Putih, 0xE = Kuning
    syscall(1, (uint32_t)str, len, 0xE);
}

int main(void) {
    print_user("RendangOS: Welcome to User Mode (Ring 3)!\n");
    print_user("Shell is running safely at 0x400000.\n");

    while (1) {
        // Nanti di sini kita baca keyboard
    }
    return 0;
}