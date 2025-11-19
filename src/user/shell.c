#include <stdint.h>
#include <stdbool.h>
#include "header/filesystem/ext2.h"

// Wrapper Syscall
void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("mov %0, %%ebx" : /* <Empty> */ : "r"(ebx));
    __asm__ volatile("mov %0, %%ecx" : /* <Empty> */ : "r"(ecx));
    __asm__ volatile("mov %0, %%edx" : /* <Empty> */ : "r"(edx));
    __asm__ volatile("mov %0, %%eax" : /* <Empty> */ : "r"(eax));
    __asm__ volatile("int $0x80");
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

int main(void) {
    syscall(7, 0, 0, 0); // Activate Keyboard
    
    puts("RendangOS Shell v1.0\n", 0xB); 
    puts("Welcome, User!\n", 0xB);

    char input_buf[128];
    int input_idx = 0;
    char cwd[128] = "/"; 

    while (true) {
        puts("User@OS:", 0xA); 
        puts(cwd, 0xE);        
        puts("$ ", 0xF);       

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
                        // TODO: Handle visual backspace
                    }
                } else {
                    if (input_idx < 127) {
                        input_buf[input_idx++] = c;
                        syscall(5, (uint32_t)c, 0xF, 0); 
                    }
                }
            }
        }

        if (streq(input_buf, "cd")) {
            puts("Feature 'cd' not implemented yet.\n", 0xC);
        } 
        else if (streq(input_buf, "ls")) {
            struct EXT2DirectoryEntry dir_buf[16]; 
            struct EXT2DriverRequest req = {
                .parent_inode = 2, // Root
                .buf = dir_buf,
                .buffer_size = sizeof(dir_buf),
            };
            int8_t ret;
            syscall(1, (uint32_t)&req, (uint32_t)&ret, 0);
            
            if (ret == 0) {
                puts("Directory listing:\n", 0xF);
                // Parsing manual simpel untuk ls
                uint8_t *ptr = (uint8_t*)dir_buf;
                uint32_t offset = 0;
                while (offset < 512) { // Asumsi 1 blok buffer
                    struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry*)(ptr + offset);
                    if (entry->inode != 0 && entry->rec_len != 0) {
                         char *name = (char*)(ptr + offset + 8);
                         for(int i=0; i<entry->name_len; i++) {
                             char s[2] = {name[i], 0};
                             puts(s, 0xE);
                         }
                         puts("\n", 0xF);
                    }
                    if (entry->rec_len == 0) break;
                    offset += entry->rec_len;
                }
            } else {
                puts("Error reading directory.\n", 0xC);
            }
        }
        else if (streq(input_buf, "mkdir")) {
            puts("Usage: mkdir <name>\n", 0xF);
        }
        else if (streq(input_buf, "clear")) {
            puts("\n\n\n\n\n", 0xF); 
        }
        else {
            puts("Unknown command: ", 0xC);
            puts(input_buf, 0xC);
            puts("\n", 0xC);
        }
        input_idx = 0;
    }
    return 0;
}