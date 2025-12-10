/**
 * RendangOS Shell - User Mode CLI
 * IF2130 Sistem Operasi - Chapter 3
 * 
 * Supported Commands:
 * - cd <path>     : Change directory
 * - ls            : List directory contents
 * - mkdir <name>  : Create directory
 * - cat <file>    : Display file contents
 * - cp <src> <dst>: Copy file
 * - rm <name>     : Remove file/directory
 * - mv <src> <dst>: Move/rename file
 * - clear         : Clear screen
 * - help          : Show help
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// SYSCALL INTERFACE
#define SYSCALL_READ           0
#define SYSCALL_READ_DIR       1
#define SYSCALL_WRITE          2
#define SYSCALL_DELETE         3
#define SYSCALL_GETCHAR        4
#define SYSCALL_PUTCHAR        5
#define SYSCALL_PUTS           6
#define SYSCALL_ACTIVATE_KBD   7
#define SYSCALL_STAT           8

// Colors
#define WHITE   0xF
#define GREEN   0xA
#define YELLOW  0xE
#define RED     0xC
#define CYAN    0xB

// EXT2 Directory Entry (simplified)
struct EXT2DirectoryEntry {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    // name follows (variable length)
} __attribute__((packed));

// Driver Request
struct EXT2DriverRequest {
    void     *buf;
    char     *name;
    uint8_t   name_len;
    uint32_t  parent_inode;
    uint32_t  buffer_size;
    bool      is_directory;
} __attribute__((packed));

// Syscall wrapper
void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("mov %0, %%ebx" : : "r"(ebx));
    __asm__ volatile("mov %0, %%ecx" : : "r"(ecx));
    __asm__ volatile("mov %0, %%edx" : : "r"(edx));
    __asm__ volatile("mov %0, %%eax" : : "r"(eax));
    __asm__ volatile("int $0x80");
}

// STRING UTILITIES
uint32_t strlen(const char *str) {
    uint32_t len = 0;
    while (str[len]) len++;
    return len;
}

bool streq(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *s1 == *s2;
}

bool strncmp(const char *s1, const char *s2, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return false;
        if (s1[i] == 0) break;
    }
    return true;
}

void strcpy(char *dst, const char *src) {
    while (*src) *dst++ = *src++;
    *dst = 0;
}

void strcat(char *dst, const char *src) {
    while (*dst) dst++;
    while (*src) *dst++ = *src++;
    *dst = 0;
}

void memset(void *ptr, int val, uint32_t size) {
    uint8_t *p = (uint8_t*)ptr;
    for (uint32_t i = 0; i < size; i++) p[i] = val;
}

void memcpy(void *dst, const void *src, uint32_t size) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (uint32_t i = 0; i < size; i++) d[i] = s[i];
}

// PIPELINE SYSTEM - Output/Input Buffering

#define PIPE_BUFFER_SIZE 4096

static char pipe_output_buffer[PIPE_BUFFER_SIZE];  // Buffer for piped output
static uint32_t pipe_output_len = 0;               // Current output length

static char pipe_input_buffer[PIPE_BUFFER_SIZE];   // Buffer for piped input
static uint32_t pipe_input_len = 0;                // Input buffer length
static uint32_t pipe_input_pos = 0;                // Current read position

static bool pipe_mode_output = false;  // If true, output goes to buffer
static bool pipe_mode_input = false;   // If true, read from pipe buffer

void pipe_reset(void) {
    pipe_output_len = 0;
    pipe_input_len = 0;
    pipe_input_pos = 0;
    pipe_mode_output = false;
    pipe_mode_input = false;
}

void pipe_set_output_mode(bool enable) {
    pipe_mode_output = enable;
    if (enable) {
        pipe_output_len = 0;
        memset(pipe_output_buffer, 0, PIPE_BUFFER_SIZE);
    }
}

void pipe_transfer_output_to_input(void) {
    // Transfer output buffer to input buffer for next command
    memcpy(pipe_input_buffer, pipe_output_buffer, pipe_output_len);
    pipe_input_len = pipe_output_len;
    pipe_input_pos = 0;
    pipe_output_len = 0;
}

void pipe_set_input_mode(bool enable) {
    pipe_mode_input = enable;
    if (enable) {
        pipe_input_pos = 0;
    }
}

// I/O FUNCTIONS (with Pipeline support)

void puts(const char *str, uint8_t color) {
    if (pipe_mode_output) {
        // Write to pipe buffer instead of screen
        uint32_t len = strlen(str);
        for (uint32_t i = 0; i < len && pipe_output_len < PIPE_BUFFER_SIZE - 1; i++) {
            pipe_output_buffer[pipe_output_len++] = str[i];
        }
    } else {
        syscall(SYSCALL_PUTS, (uint32_t)str, strlen(str), color);
    }
}

void putchar(char c, uint8_t color) {
    if (pipe_mode_output) {
        if (pipe_output_len < PIPE_BUFFER_SIZE - 1) {
            pipe_output_buffer[pipe_output_len++] = c;
        }
    } else {
        syscall(SYSCALL_PUTCHAR, (uint32_t)c, color, 0);
    }
}

// Read a character - from keyboard or pipe buffer
char getchar(void) {
    if (pipe_mode_input) {
        // Read from pipe buffer
        if (pipe_input_pos < pipe_input_len) {
            return pipe_input_buffer[pipe_input_pos++];
        }
        return 0;  // End of pipe input
    } else {
        char c = 0;
        syscall(SYSCALL_GETCHAR, (uint32_t)&c, 0, 0);
        return c;
    }
}

// Read a line from pipe input (for piped commands)
bool pipe_getline(char *buf, uint32_t max_len) {
    if (!pipe_mode_input || pipe_input_pos >= pipe_input_len) {
        return false;
    }
    
    uint32_t i = 0;
    while (pipe_input_pos < pipe_input_len && i < max_len - 1) {
        char c = pipe_input_buffer[pipe_input_pos++];
        if (c == '\n') {
            break;
        }
        buf[i++] = c;
    }
    buf[i] = 0;
    return i > 0 || pipe_input_pos < pipe_input_len;
}

void print_int(uint32_t num, uint8_t color) {
    char buf[12];
    int i = 0;
    if (num == 0) {
        putchar('0', color);
        return;
    }
    while (num > 0) {
        buf[i++] = '0' + (num % 10);
        num /= 10;
    }
    while (i > 0) {
        putchar(buf[--i], color);
    }
}

// FILE STAT HELPER
struct StatRequest {
    char *name;
    uint8_t name_len;
    uint32_t parent_inode;
} __attribute__((packed));

int32_t get_file_size(const char *filename, uint32_t parent_inode) {
    struct StatRequest req = {
        .name = (char*)filename,
        .name_len = strlen(filename),
        .parent_inode = parent_inode
    };
    uint32_t size = 0;
    bool is_dir = false;
    int32_t ret;
    
    // Call SYSCALL_STAT: EBX=req, ECX=size_out, EDX=is_dir_out
    // Return value in EAX
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYSCALL_STAT), "b"(&req), "c"(&size), "d"(&is_dir)
    );
    
    if (ret != 0) return -1;
    return (int32_t)size;
}

// SHELL STATE
static uint32_t current_dir_inode = 2;  // Root inode
static char cwd[256] = "/";
static char input_buf[256];
static char arg1[128];
static char arg2[128];

// Buffers for filesystem operations
static uint8_t dir_buffer[512];
static uint8_t file_buffer[32768];  // 32KB for larger files

// COMMAND PARSING
// Parse input into command and arguments
void parse_input(const char *input, char *cmd, char *a1, char *a2) {
    int i = 0, j = 0;
    
    // Skip leading spaces
    while (input[i] == ' ') i++;
    
    // Get command
    while (input[i] && input[i] != ' ') {
        cmd[j++] = input[i++];
    }
    cmd[j] = 0;
    
    // Skip spaces
    while (input[i] == ' ') i++;
    
    // Get arg1
    j = 0;
    while (input[i] && input[i] != ' ') {
        a1[j++] = input[i++];
    }
    a1[j] = 0;
    
    // Skip spaces
    while (input[i] == ' ') i++;
    
    // Get arg2
    j = 0;
    while (input[i] && input[i] != ' ') {
        a2[j++] = input[i++];
    }
    a2[j] = 0;
}

// COMMAND IMPLEMENTATIONS
// cd - Change Directory
void cmd_cd(const char *path) {
    if (path[0] == 0) {
        // cd without argument - go to root
        current_dir_inode = 2;
        strcpy(cwd, "/");
        return;
    }
    
    if (streq(path, "/")) {
        current_dir_inode = 2;
        strcpy(cwd, "/");
        return;
    }
    
    if (streq(path, "..")) {
        // Go to parent (simplified - just go to root if in root)
        if (current_dir_inode != 2) {
            // Read current directory to find ".." entry
            struct EXT2DriverRequest req = {
                .buf = dir_buffer,
                .buffer_size = sizeof(dir_buffer),
                .parent_inode = current_dir_inode
            };
            int8_t ret;
            syscall(SYSCALL_READ_DIR, (uint32_t)&req, (uint32_t)&ret, 0);
            
            if (ret == 0) {
                // Find ".." entry
                uint32_t offset = 0;
                while (offset < 512) {
                    struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry*)(dir_buffer + offset);
                    if (entry->inode == 0 || entry->rec_len == 0) break;
                    
                    char *name = (char*)(dir_buffer + offset + 8);
                    if (entry->name_len == 2 && name[0] == '.' && name[1] == '.') {
                        current_dir_inode = entry->inode;
                        // Simplified: just show /.. for now
                        if (current_dir_inode == 2) {
                            strcpy(cwd, "/");
                        }
                        return;
                    }
                    offset += entry->rec_len;
                }
            }
        }
        return;
    }
    
    // Try to find the directory
    struct EXT2DriverRequest req = {
        .buf = dir_buffer,
        .buffer_size = sizeof(dir_buffer),
        .parent_inode = current_dir_inode
    };
    int8_t ret;
    syscall(SYSCALL_READ_DIR, (uint32_t)&req, (uint32_t)&ret, 0);
    
    if (ret != 0) {
        puts("cd: Cannot read directory\n", RED);
        return;
    }
    
    // Search for the path
    uint32_t offset = 0;
    while (offset < 512) {
        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry*)(dir_buffer + offset);
        if (entry->inode == 0 || entry->rec_len == 0) break;
        
        char *name = (char*)(dir_buffer + offset + 8);
        
        // Compare names
        if (entry->name_len == strlen(path)) {
            bool match = true;
            for (uint8_t i = 0; i < entry->name_len; i++) {
                if (name[i] != path[i]) { match = false; break; }
            }
            
            if (match) {
                // Check if it's a directory (file_type == 2)
                if (entry->file_type == 2) {
                    current_dir_inode = entry->inode;
                    // Update cwd
                    if (!streq(cwd, "/")) strcat(cwd, "/");
                    strcat(cwd, path);
                    return;
                } else {
                    puts("cd: Not a directory: ", RED);
                    puts(path, RED);
                    puts("\n", WHITE);
                    return;
                }
            }
        }
        offset += entry->rec_len;
    }
    
    puts("cd: No such directory: ", RED);
    puts(path, RED);
    puts("\n", WHITE);
}

// ls - List Directory
void cmd_ls(void) {
    struct EXT2DriverRequest req = {
        .buf = dir_buffer,
        .buffer_size = sizeof(dir_buffer),
        .parent_inode = current_dir_inode
    };
    int8_t ret;
    syscall(SYSCALL_READ_DIR, (uint32_t)&req, (uint32_t)&ret, 0);
    
    if (ret != 0) {
        puts("ls: Cannot read directory\n", RED);
        return;
    }
    
    uint32_t offset = 0;
    while (offset < 512) {
        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry*)(dir_buffer + offset);
        if (entry->inode == 0 || entry->rec_len == 0) break;
        
        char *name = (char*)(dir_buffer + offset + 8);
        
        // Print name
        for (uint8_t i = 0; i < entry->name_len; i++) {
            // Directory in cyan, file in white
            putchar(name[i], (entry->file_type == 2) ? CYAN : WHITE);
        }
        
        if (entry->file_type == 2) {
            puts("/", CYAN);
        }
        puts("  ", WHITE);
        
        offset += entry->rec_len;
    }
    puts("\n", WHITE);
}

// mkdir - Make Directory
void cmd_mkdir(const char *name) {
    if (name[0] == 0) {
        puts("mkdir: Missing operand\n", RED);
        return;
    }
    
    struct EXT2DriverRequest req = {
        .buf = NULL,
        .name = (char*)name,
        .name_len = strlen(name),
        .parent_inode = current_dir_inode,
        .buffer_size = 0,
        .is_directory = true
    };
    
    int8_t ret;
    syscall(SYSCALL_WRITE, (uint32_t)&req, (uint32_t)&ret, 0);
    
    if (ret == 0) {
        puts("Directory created: ", GREEN);
        puts(name, GREEN);
        puts("\n", WHITE);
    } else if (ret == 1) {
        puts("mkdir: Directory already exists\n", RED);
    } else {
        puts("mkdir: Failed to create directory\n", RED);
    }
}

// cat - Display File
void cmd_cat(const char *filename) {
    if (filename[0] == 0) {
        puts("cat: Missing operand\n", RED);
        return;
    }
    
    // Get actual file size
    int32_t file_size = get_file_size(filename, current_dir_inode);
    if (file_size < 0) {
        puts("cat: File not found: ", RED);
        puts(filename, RED);
        puts("\n", WHITE);
        return;
    }
    
    if ((uint32_t)file_size > sizeof(file_buffer)) {
        puts("cat: File too large\n", RED);
        return;
    }
    
    struct EXT2DriverRequest req = {
        .buf = file_buffer,
        .name = (char*)filename,
        .name_len = strlen(filename),
        .parent_inode = current_dir_inode,
        .buffer_size = (uint32_t)file_size,
        .is_directory = false
    };
    
    int8_t ret;
    syscall(SYSCALL_READ, (uint32_t)&req, (uint32_t)&ret, 0);
    
    if (ret == 0) {
        // Print file contents byte by byte, handling LF newlines
        for (int32_t i = 0; i < file_size; i++) {
            char c = (char)file_buffer[i];
            if (c == '\r') continue;  // Skip CR in CRLF
            putchar(c, WHITE);
        }
        // Ensure newline at end if file doesn't end with one
        if (file_size > 0 && file_buffer[file_size - 1] != '\n') {
            puts("\n", WHITE);
        }
    } else if (ret == 1) {
        puts("cat: Is a directory\n", RED);
    } else {
        puts("cat: Error reading file\n", RED);
    }
}

// cp - Copy File
void cmd_cp(const char *src, const char *dst) {
    if (src[0] == 0 || dst[0] == 0) {
        puts("cp: Missing operand\n", RED);
        puts("Usage: cp <source> <destination>\n", YELLOW);
        return;
    }
    
    // Get actual file size first
    int32_t file_size = get_file_size(src, current_dir_inode);
    if (file_size < 0) {
        puts("cp: Cannot find source: ", RED);
        puts(src, RED);
        puts("\n", WHITE);
        return;
    }
    
    if ((uint32_t)file_size > sizeof(file_buffer)) {
        puts("cp: File too large\n", RED);
        return;
    }
    
    // Read source file
    struct EXT2DriverRequest read_req = {
        .buf = file_buffer,
        .name = (char*)src,
        .name_len = strlen(src),
        .parent_inode = current_dir_inode,
        .buffer_size = (uint32_t)file_size,
        .is_directory = false
    };
    
    int8_t ret;
    syscall(SYSCALL_READ, (uint32_t)&read_req, (uint32_t)&ret, 0);
    
    if (ret != 0) {
        puts("cp: Cannot read source file: ", RED);
        puts(src, RED);
        puts("\n", WHITE);
        return;
    }
    
    uint32_t size = (uint32_t)file_size;
    
    // Write destination file
    struct EXT2DriverRequest write_req = {
        .buf = file_buffer,
        .name = (char*)dst,
        .name_len = strlen(dst),
        .parent_inode = current_dir_inode,
        .buffer_size = size,
        .is_directory = false
    };
    
    syscall(SYSCALL_WRITE, (uint32_t)&write_req, (uint32_t)&ret, 0);
    
    if (ret == 0) {
        puts("Copied: ", GREEN);
        puts(src, GREEN);
        puts(" -> ", WHITE);
        puts(dst, GREEN);
        puts("\n", WHITE);
    } else if (ret == 1) {
        puts("cp: Destination already exists: ", RED);
        puts(dst, RED);
        puts("\n", WHITE);
    } else {
        puts("cp: Failed to copy\n", RED);
    }
}

// rm - Remove File/Directory
void cmd_rm(const char *name) {
    if (name[0] == 0) {
        puts("rm: Missing operand\n", RED);
        return;
    }
    
    struct EXT2DriverRequest req = {
        .name = (char*)name,
        .name_len = strlen(name),
        .parent_inode = current_dir_inode,
        .is_directory = false  // Try file first
    };
    
    int8_t ret;
    syscall(SYSCALL_DELETE, (uint32_t)&req, (uint32_t)&ret, 0);
    
    if (ret == 1) {
        // Maybe it's a directory?
        req.is_directory = true;
        syscall(SYSCALL_DELETE, (uint32_t)&req, (uint32_t)&ret, 0);
    }
    
    if (ret == 0) {
        puts("Removed: ", GREEN);
        puts(name, GREEN);
        puts("\n", WHITE);
    } else if (ret == 1) {
        puts("rm: No such file or directory: ", RED);
        puts(name, RED);
        puts("\n", WHITE);
    } else if (ret == 2) {
        puts("rm: Directory not empty\n", RED);
    } else {
        puts("rm: Failed to remove\n", RED);
    }
}

// Helper: Check if name is a directory in current dir, returns inode or 0
uint32_t is_directory(const char *name) {
    struct StatRequest req = {
        .name = (char*)name,
        .name_len = strlen(name),
        .parent_inode = current_dir_inode
    };
    uint32_t size = 0;
    bool is_dir = false;
    int32_t ret;
    
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYSCALL_STAT), "b"(&req), "c"(&size), "d"(&is_dir)
    );
    
    if (ret == 0 && is_dir) {
        // Need to get inode - read directory to find it
        struct EXT2DriverRequest dir_req = {
            .buf = dir_buffer,
            .parent_inode = current_dir_inode,
            .buffer_size = sizeof(dir_buffer),
            .is_directory = true
        };
        syscall(SYSCALL_READ_DIR, (uint32_t)&dir_req, (uint32_t)&ret, 0);
        
        if (ret == 0) {
            uint32_t offset = 0;
            while (offset < sizeof(dir_buffer)) {
                struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)(dir_buffer + offset);
                if (entry->rec_len == 0) break;
                if (entry->inode != 0) {
                    char *entry_name = (char*)(entry + 1);
                    bool match = true;
                    uint8_t name_len = strlen(name);
                    if (entry->name_len == name_len) {
                        for (uint8_t i = 0; i < name_len; i++) {
                            if (entry_name[i] != name[i]) {
                                match = false;
                                break;
                            }
                        }
                        if (match && entry->file_type == 2) {
                            return entry->inode;
                        }
                    }
                }
                offset += entry->rec_len;
            }
        }
    }
    return 0;
}

// mv - Move/Rename
void cmd_mv(const char *src, const char *dst) {
    if (src[0] == 0 || dst[0] == 0) {
        puts("mv: Missing operand\n", RED);
        puts("Usage: mv <source> <destination>\n", YELLOW);
        return;
    }
    
    // Check if destination is a directory
    uint32_t dst_dir_inode = is_directory(dst);
    uint32_t target_parent = current_dir_inode;
    char target_name[128];
    
    if (dst_dir_inode != 0) {
        // Destination is a directory - move file INTO it
        target_parent = dst_dir_inode;
        strcpy(target_name, src);  // Keep original filename
    } else {
        // Destination is a new filename
        target_parent = current_dir_inode;
        strcpy(target_name, dst);
    }
    
    // Get actual file size first
    int32_t file_size = get_file_size(src, current_dir_inode);
    if (file_size < 0) {
        puts("mv: Cannot find source: ", RED);
        puts(src, RED);
        puts("\n", WHITE);
        return;
    }
    
    if (file_size == 0) {
        puts("mv: Source file is empty or error\n", RED);
        return;
    }
    
    if ((uint32_t)file_size > sizeof(file_buffer)) {
        puts("mv: File too large\n", RED);
        return;
    }
    
    // Read source file
    struct EXT2DriverRequest read_req = {
        .buf = file_buffer,
        .name = (char*)src,
        .name_len = strlen(src),
        .parent_inode = current_dir_inode,
        .buffer_size = (uint32_t)file_size,
        .is_directory = false
    };
    
    int8_t ret;
    syscall(SYSCALL_READ, (uint32_t)&read_req, (uint32_t)&ret, 0);
    
    if (ret != 0) {
        puts("mv: Cannot read source: ", RED);
        puts(src, RED);
        puts("\n", WHITE);
        return;
    }
    
    // Write to destination with actual size
    struct EXT2DriverRequest write_req = {
        .buf = file_buffer,
        .name = target_name,
        .name_len = strlen(target_name),
        .parent_inode = target_parent,
        .buffer_size = (uint32_t)file_size,
        .is_directory = false
    };
    
    syscall(SYSCALL_WRITE, (uint32_t)&write_req, (uint32_t)&ret, 0);
    
    if (ret == 1) {
        // File exists in destination, delete it first
        struct EXT2DriverRequest del_dst = {
            .name = target_name,
            .name_len = strlen(target_name),
            .parent_inode = target_parent,
            .is_directory = false
        };
        int8_t del_ret;
        syscall(SYSCALL_DELETE, (uint32_t)&del_dst, (uint32_t)&del_ret, 0);
        
        // Retry write
        syscall(SYSCALL_WRITE, (uint32_t)&write_req, (uint32_t)&ret, 0);
    }
    
    if (ret != 0) {
        puts("mv: Cannot write destination (error ", RED);
        print_int(ret, RED);
        puts(")\n", RED);
        return;
    }
    
    // Delete source
    struct EXT2DriverRequest del_req = {
        .name = (char*)src,
        .name_len = strlen(src),
        .parent_inode = current_dir_inode,
        .is_directory = false
    };
    
    syscall(SYSCALL_DELETE, (uint32_t)&del_req, (uint32_t)&ret, 0);
    
    puts("Moved: ", GREEN);
    puts(src, GREEN);
    puts(" -> ", WHITE);
    if (dst_dir_inode != 0) {
        puts(dst, GREEN);
        puts("/", GREEN);
        puts(target_name, GREEN);
    } else {
        puts(dst, GREEN);
    }
    puts("\n", WHITE);
}

// clear - Clear Screen (using newlines)
void cmd_clear(void) {
    for (int i = 0; i < 25; i++) {
        puts("\n", WHITE);
    }
}

// find - Search for files/directories using DFS graph traversal
// Stack-based DFS to avoid recursion (limited stack in user mode)
#define FIND_MAX_DEPTH 32

static uint32_t find_stack[FIND_MAX_DEPTH];  // Stack of inode numbers
static char find_paths[FIND_MAX_DEPTH][128]; // Stack of paths
static int find_sp = 0;  // Stack pointer

void find_push(uint32_t inode, const char *path) {
    if (find_sp < FIND_MAX_DEPTH) {
        find_stack[find_sp] = inode;
        strcpy(find_paths[find_sp], path);
        find_sp++;
    }
}

bool find_pop(uint32_t *inode, char *path) {
    if (find_sp > 0) {
        find_sp--;
        *inode = find_stack[find_sp];
        strcpy(path, find_paths[find_sp]);
        return true;
    }
    return false;
}

void cmd_find(const char *search_name) {
    if (search_name[0] == 0) {
        puts("find: Missing search term\n", RED);
        puts("Usage: find <filename>\n", YELLOW);
        return;
    }
    
    puts("Searching for '", WHITE);
    puts(search_name, YELLOW);
    puts("' in entire filesystem...\n", WHITE);
    
    // Initialize DFS from root
    find_sp = 0;
    find_push(2, "/");  // Start from root (inode 2)
    
    int found_count = 0;
    uint32_t current_inode;
    char current_path[128];
    
    // DFS traversal
    while (find_pop(&current_inode, current_path)) {
        // Read directory entries
        struct EXT2DriverRequest req = {
            .buf = dir_buffer,
            .parent_inode = current_inode,
            .buffer_size = sizeof(dir_buffer),
            .is_directory = true
        };
        
        int8_t ret;
        syscall(SYSCALL_READ_DIR, (uint32_t)&req, (uint32_t)&ret, 0);
        
        if (ret != 0) continue;
        
        // Parse directory entries
        uint32_t offset = 0;
        while (offset < sizeof(dir_buffer)) {
            struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)(dir_buffer + offset);
            if (entry->rec_len == 0) break;
            if (entry->inode == 0) {
                offset += entry->rec_len;
                continue;
            }
            
            // Get entry name
            char *entry_name = (char*)(entry + 1);
            char name_buf[64];
            memset(name_buf, 0, sizeof(name_buf));
            memcpy(name_buf, entry_name, entry->name_len);
            
            // Skip . and ..
            if (streq(name_buf, ".") || streq(name_buf, "..")) {
                offset += entry->rec_len;
                continue;
            }
            
            // Build full path
            char full_path[128];
            strcpy(full_path, current_path);
            if (full_path[strlen(full_path) - 1] != '/') {
                strcat(full_path, "/");
            }
            strcat(full_path, name_buf);
            
            // Check if name matches search term
            if (streq(name_buf, search_name)) {
                puts("  Found: ", GREEN);
                puts(full_path, YELLOW);
                if (entry->file_type == 2) {  // Directory
                    puts("/", CYAN);
                }
                puts("\n", WHITE);
                found_count++;
            }
            
            // If directory, push to stack for DFS
            if (entry->file_type == 2) {  // EXT2_FT_DIR = 2
                find_push(entry->inode, full_path);
            }
            
            offset += entry->rec_len;
        }
    }
    
    if (found_count == 0) {
        puts("No matches found.\n", YELLOW);
    } else {
        puts("Found ", GREEN);
        print_int(found_count, GREEN);
        puts(" match(es).\n", GREEN);
    }
}

// PIPELINE COMMANDS (grep, wc)

// Check if string contains substring
bool str_contains(const char *haystack, const char *needle) {
    if (!needle[0]) return true;
    
    uint32_t needle_len = strlen(needle);
    uint32_t haystack_len = strlen(haystack);
    
    if (needle_len > haystack_len) return false;
    
    for (uint32_t i = 0; i <= haystack_len - needle_len; i++) {
        bool match = true;
        for (uint32_t j = 0; j < needle_len; j++) {
            if (haystack[i + j] != needle[j]) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

// grep - Filter lines containing pattern (from file or pipe)
void cmd_grep(const char *pattern, const char *filename) {
    if (pattern[0] == 0) {
        puts("grep: Missing pattern\n", RED);
        puts("Usage: grep <pattern> [file] or cmd | grep <pattern>\n", YELLOW);
        return;
    }
    
    char line[256];
    
    if (pipe_mode_input) {
        // Read from pipe
        while (pipe_getline(line, sizeof(line))) {
            if (str_contains(line, pattern)) {
                puts(line, WHITE);
                puts("\n", WHITE);
            }
        }
    } else if (filename[0] != 0) {
        // Read from file
        int32_t file_size = get_file_size(filename, current_dir_inode);
        if (file_size < 0) {
            puts("grep: File not found: ", RED);
            puts(filename, RED);
            puts("\n", WHITE);
            return;
        }
        
        if ((uint32_t)file_size > sizeof(file_buffer)) {
            puts("grep: File too large\n", RED);
            return;
        }
        
        struct EXT2DriverRequest req = {
            .buf = file_buffer,
            .name = (char*)filename,
            .name_len = strlen(filename),
            .parent_inode = current_dir_inode,
            .buffer_size = (uint32_t)file_size,
            .is_directory = false
        };
        
        int8_t ret;
        syscall(SYSCALL_READ, (uint32_t)&req, (uint32_t)&ret, 0);
        
        if (ret != 0) {
            puts("grep: Error reading file\n", RED);
            return;
        }
        
        // Process file line by line
        uint32_t line_start = 0;
        uint32_t line_len = 0;
        
        for (int32_t i = 0; i <= file_size; i++) {
            if (i == file_size || file_buffer[i] == '\n') {
                // End of line
                if (line_len > 0 && line_len < sizeof(line)) {
                    memcpy(line, file_buffer + line_start, line_len);
                    line[line_len] = 0;
                    
                    if (str_contains(line, pattern)) {
                        puts(line, WHITE);
                        puts("\n", WHITE);
                    }
                }
                line_start = i + 1;
                line_len = 0;
            } else {
                line_len++;
            }
        }
    } else {
        puts("grep: No input (use: grep <pattern> <file> or cmd | grep <pattern>)\n", RED);
    }
}

// echo - Create a text file with content
void cmd_echo(const char *text, const char *filename) {
    if (text[0] == 0) {
        puts("echo: Missing text\n", RED);
        puts("Usage: echo <text> <filename>\n", YELLOW);
        return;
    }
    
    if (filename[0] == 0) {
        // Just print to screen
        puts(text, WHITE);
        puts("\n", WHITE);
        return;
    }
    
    // Create file with text content
    uint32_t len = strlen(text);
    memcpy(file_buffer, text, len);
    file_buffer[len] = '\n';  // Add newline
    
    struct EXT2DriverRequest req = {
        .buf = file_buffer,
        .name = (char*)filename,
        .name_len = strlen(filename),
        .parent_inode = current_dir_inode,
        .buffer_size = len + 1,
        .is_directory = false
    };
    
    int8_t ret;
    syscall(SYSCALL_WRITE, (uint32_t)&req, (uint32_t)&ret, 0);
    
    if (ret == 0) {
        puts("Created: ", GREEN);
        puts(filename, GREEN);
        puts("\n", WHITE);
    } else if (ret == 1) {
        puts("echo: File already exists\n", RED);
    } else {
        puts("echo: Failed to create file\n", RED);
    }
}

// wc - Word/line/char count
void cmd_wc(const char *flags) {
    uint32_t lines = 0;
    uint32_t words = 0;
    uint32_t chars = 0;
    bool in_word = false;
    
    if (pipe_mode_input) {
        // Count from pipe buffer
        for (uint32_t i = 0; i < pipe_input_len; i++) {
            char c = pipe_input_buffer[i];
            chars++;
            
            if (c == '\n') {
                lines++;
                in_word = false;
            } else if (c == ' ' || c == '\t') {
                in_word = false;
            } else {
                if (!in_word) {
                    words++;
                    in_word = true;
                }
            }
        }
    } else {
        puts("wc: No pipe input (use with |)\n", RED);
        return;
    }
    
    // Output based on flags (default: all)
    bool show_lines = (flags[0] == 0 || flags[0] == 'l');
    bool show_words = (flags[0] == 0 || flags[0] == 'w');
    bool show_chars = (flags[0] == 0 || flags[0] == 'c');
    
    if (show_lines) {
        print_int(lines, WHITE);
        puts(" ", WHITE);
    }
    if (show_words) {
        print_int(words, WHITE);
        puts(" ", WHITE);
    }
    if (show_chars) {
        print_int(chars, WHITE);
    }
    puts("\n", WHITE);
}

// testuser - Test usermode by trying privileged instructions
void cmd_testuser(void) {
    puts("========================================\n", CYAN);
    puts("       USER MODE (Ring 3) TEST\n", CYAN);
    puts("========================================\n", CYAN);
    puts("\n", WHITE);
    puts("This test will try to execute CLI instruction.\n", WHITE);
    puts("CLI is a PRIVILEGED instruction (Ring 0 only).\n", WHITE);
    puts("\n", WHITE);
    puts("Expected behavior:\n", YELLOW);
    puts("  Ring 0 (Kernel): CLI succeeds, message appears\n", WHITE);
    puts("  Ring 3 (User):   CPU raises GPF, system FREEZES\n", WHITE);
    puts("\n", WHITE);
    puts("If system FREEZES after keypress = WE ARE IN USER MODE!\n", GREEN);
    puts("(This is CORRECT behavior - proves Ring 3)\n", GREEN);
    puts("\n", WHITE);
    puts("Press any key to execute CLI...\n", RED);
    
    // Wait for keypress
    char c = 0;
    while (c == 0) {
        c = getchar();
    }
    
    puts("Executing CLI...\n", YELLOW);
    
    // Try to execute CLI - this is a privileged instruction
    // In Ring 3 (user mode), this will cause a General Protection Fault (#GP)
    __asm__ volatile("cli");
    
    // If we get here, we're NOT in user mode (which would be bad!)
    puts("\n", WHITE);
    puts("!!! WARNING: CLI SUCCEEDED !!!\n", RED);
    puts("This means we are NOT in user mode!\n", RED);
    puts("Shell should be running in Ring 3.\n", RED);
}

// quit - Exit shell (halt CPU)
void cmd_quit(void) {
    puts("\n", WHITE);
    puts("Goodbye! Halting system...\n", CYAN);
    puts("You can safely close QEMU now.\n", YELLOW);
    puts("\n", WHITE);
    
    // Halt the CPU - infinite loop
    while (1) {
        __asm__ volatile("hlt");
    }
}

// help - Show Help
void cmd_help(void) {
    puts("RendangOS Shell Commands:\n", CYAN);
    puts("  cd <dir>        - Change directory\n", WHITE);
    puts("  ls              - List directory contents\n", WHITE);
    puts("  mkdir <name>    - Create directory\n", WHITE);
    puts("  cat <file>      - Display file contents\n", WHITE);
    puts("  cp <src> <dst>  - Copy file\n", WHITE);
    puts("  rm <name>       - Remove file/directory\n", WHITE);
    puts("  mv <src> <dst>  - Move/rename file\n", WHITE);
    puts("  echo <txt> <f>  - Create file with text\n", WHITE);
    puts("  find <name>     - Search filesystem (DFS graph)\n", WHITE);
    puts("  grep <p> [file] - Search pattern in file/pipe\n", WHITE);
    puts("  wc              - Count lines/words/chars\n", WHITE);
    puts("  clear           - Clear screen\n", WHITE);
    puts("  pwd             - Print working directory\n", WHITE);
    puts("  exit/quit       - Exit shell\n", WHITE);
    puts("  help            - Show this help\n", WHITE);
    puts("\nPipeline: cmd1 | cmd2 | cmd3\n", YELLOW);
    puts("Example: ls | grep test\n", YELLOW);
}

// MAIN SHELL LOOP

int main(void) {
    // Activate keyboard
    syscall(SYSCALL_ACTIVATE_KBD, 0, 0, 0);
    
    // Welcome message
    puts("\n", WHITE);
    puts("======================================\n", CYAN);
    puts("     RendangOS Shell v1.0\n", GREEN);
    puts("     Running in USER MODE (Ring 3)\n", YELLOW);
    puts("     Type 'help' for commands\n", WHITE);
    puts("======================================\n", CYAN);
    puts("\n", WHITE);
    
    char cmd[64];
    int input_idx = 0;

    while (true) {
        // Print prompt
        puts("User@RendangOS:", GREEN);
        puts(cwd, YELLOW);
        puts("$ ", WHITE);
        
        // Read input
        input_idx = 0;
        memset(input_buf, 0, sizeof(input_buf));

        while (true) {
            char c = getchar();
            
            if (c) {
                if (c == '\n') {
                    puts("\n", WHITE);
                    input_buf[input_idx] = 0;
                    break;
                } else if (c == '\b') {
                    if (input_idx > 0) {
                        input_idx--;
                        input_buf[input_idx] = 0;
                        putchar('\b', WHITE);  // Kernel handles visual backspace
                    }
                } else if (input_idx < 255) {
                        input_buf[input_idx++] = c;
                    putchar(c, WHITE);
                }
            }
        }
        
        // Skip empty input
        if (input_buf[0] == 0) continue;
        
        // Reset pipeline state
        pipe_reset();
        
        // Check for pipe character and split commands
        char *pipe_cmds[8];  // Max 8 piped commands
        int pipe_count = 0;
        char *p = input_buf;
        pipe_cmds[pipe_count++] = p;
        
        while (*p) {
            if (*p == '|') {
                *p = 0;  // Terminate current command
                p++;
                while (*p == ' ') p++;  // Skip spaces
                if (*p && pipe_count < 8) {
                    pipe_cmds[pipe_count++] = p;
                }
            } else {
                p++;
            }
        }
        
        // Execute each command in pipeline
        for (int pipe_idx = 0; pipe_idx < pipe_count; pipe_idx++) {
            // Parse current command
            memset(cmd, 0, sizeof(cmd));
            memset(arg1, 0, sizeof(arg1));
            memset(arg2, 0, sizeof(arg2));
            parse_input(pipe_cmds[pipe_idx], cmd, arg1, arg2);
            
            // Set up pipeline modes
            if (pipe_count > 1) {
                // If not last command, redirect output to pipe buffer
                if (pipe_idx < pipe_count - 1) {
                    pipe_set_output_mode(true);
                } else {
                    pipe_set_output_mode(false);
                }
                
                // If not first command, read from pipe buffer
                if (pipe_idx > 0) {
                    pipe_set_input_mode(true);
                }
            }
            
            // Execute command
            if (streq(cmd, "cd")) {
                cmd_cd(arg1);
            } else if (streq(cmd, "ls")) {
                cmd_ls();
            } else if (streq(cmd, "mkdir")) {
                cmd_mkdir(arg1);
            } else if (streq(cmd, "cat")) {
                cmd_cat(arg1);
            } else if (streq(cmd, "cp")) {
                cmd_cp(arg1, arg2);
            } else if (streq(cmd, "rm")) {
                cmd_rm(arg1);
            } else if (streq(cmd, "mv")) {
                cmd_mv(arg1, arg2);
            } else if (streq(cmd, "find")) {
                cmd_find(arg1);
            } else if (streq(cmd, "grep")) {
                cmd_grep(arg1, arg2);
            } else if (streq(cmd, "wc")) {
                cmd_wc(arg1);
            } else if (streq(cmd, "echo")) {
                cmd_echo(arg1, arg2);
            } else if (streq(cmd, "clear")) {
                cmd_clear();
            } else if (streq(cmd, "help")) {
                cmd_help();
            } else if (streq(cmd, "quit") || streq(cmd, "exit")) {
                cmd_quit();
            } else if (streq(cmd, "testuser")) {
                cmd_testuser();
            } else if (streq(cmd, "pwd")) {
                puts(cwd, YELLOW);
                puts("\n", WHITE);
            } else if (cmd[0] != 0) {
                puts("Unknown command: ", RED);
                puts(cmd, RED);
                puts("\n", WHITE);
                puts("Type 'help' for available commands.\n", YELLOW);
            }
            
            // Transfer output to input for next command in pipeline
            if (pipe_idx < pipe_count - 1) {
                pipe_transfer_output_to_input();
            }
        }
        
        // Reset pipeline after execution
        pipe_reset();
    }
    
    return 0;
}
