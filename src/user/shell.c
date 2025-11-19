// #include <stdint.h>

// // Wrapper Syscall Manual (karena gak ada stdlib)
// void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
//     __asm__ volatile("int $0x80" : : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx));
// }

// void print_user(char *str) {
//     // Cari panjang string manual
//     int len = 0;
//     while (str[len]) len++;
    
//     // Syscall Write (EAX=1, EBX=str, ECX=len, EDX=Warna)
//     // Warna 0xF = Putih, 0xE = Kuning
//     syscall(1, (uint32_t)str, len, 0xE);
// }

// int main(void) {
//     print_user("RendangOS: Welcome to User Mode (Ring 3)!\n");
//     print_user("Shell is running safely at 0x400000.\n");

//     while (1) {
//         // Nanti di sini kita baca keyboard
//     }
//     return 0;
// }

#include <stdint.h>
#include <stdbool.h>
#include "header/filesystem/ext2.h" // Butuh struct EXT2DriverRequest

void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("int $0x80" : : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx));
}

uint32_t strlen(const char* str) {
    uint32_t len = 0;
    while (str[len]) len++;
    return len;
}

bool streq(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *s1 == *s2;
}

void puts(const char *str, uint8_t color) {
    syscall(6, (uint32_t)str, strlen(str), color);
}

void get_cwd(char* buf) {
    // TODO: Implementasi CWD tracker. Untuk awal kita hardcode "/"
    buf[0] = '/'; buf[1] = 0;
}

int main(void) {
    // 1. Aktifkan Keyboard
    syscall(7, 0, 0, 0); 
    
    puts("RendangOS Shell v1.0\n", 0xB); // 0xB = Light Cyan
    puts("Welcome, User!\n", 0xB);

    char input_buf[128];
    int input_idx = 0;
    char cwd[128] = "/"; // Current Working Directory

    while (true) {
        // Print Prompt: "User@OS:/$ "
        puts("User@OS:", 0xA); // Hijau
        puts(cwd, 0xE);        // Kuning
        puts("$ ", 0xF);       // Putih

        // Read Input Loop
        while (true) {
            char c = 0;
            syscall(4, (uint32_t)&c, 0, 0); // Getchar
            
            if (c) {
                if (c == '\n') {
                    puts("\n", 0xF);
                    input_buf[input_idx] = 0;
                    break;
                } else if (c == '\b') {
                    if (input_idx > 0) {
                        input_idx--;
                        // Hapus karakter di layar (Backspace visual)
                        // Ini butuh syscall khusus atau trik puts backspace+space+backspace
                        // Untuk simpel, kita abaikan visual backspace dulu
                    }
                } else {
                    if (input_idx < 127) {
                        input_buf[input_idx++] = c;
                        // Echo karakter ke layar
                        syscall(5, c, 0xF, 0); 
                    }
                }
            }
        }

        // Parse Command (Sangat Sederhana)
        if (streq(input_buf, "cd")) {
            puts("Feature 'cd' not implemented yet.\n", 0xC);
        } 
        else if (streq(input_buf, "ls")) {
            // Panggil Syscall Read Directory
            // Implementasikan sesuai tabel syscall: eax=1
            // Butuh struct EXT2DriverRequest
            struct EXT2DirectoryEntry dir_buf[16]; // Buffer kecil
            struct EXT2DriverRequest req = {
                .parent_inode = 2, // Root inode (sementara hardcode)
                .buf = dir_buf,
                .buffer_size = sizeof(dir_buf),
            };
            int8_t ret;
            syscall(1, (uint32_t)&req, (uint32_t)&ret, 0);
            
            if (ret == 0) {
                puts("Directory listing:\n", 0xF);
                // Loop & print entry (Butuh parsing manual struct directory entry)
            } else {
                puts("Error reading directory.\n", 0xC);
            }
        }
        else if (streq(input_buf, "mkdir")) {
            puts("Usage: mkdir <name>\n", 0xF);
        }
        else if (streq(input_buf, "clear")) {
            // TODO: Syscall clear screen
            puts("\n\n\n\n\n", 0xF); 
        }
        else {
            puts("Unknown command: ", 0xC);
            puts(input_buf, 0xC);
            puts("\n", 0xC);
        }

        // Reset buffer
        input_idx = 0;
    }

    return 0;
}