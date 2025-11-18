#include "header/cpu/gdt.h"
#include "header/cpu/interrupt.h"
#include "header/cpu/idt.h"
#include "header/driver/framebuffer.h"
#include "header/driver/keyboard.h"
#include "header/filesystem/ext2.h"
#include "header/stdlib/string.h"

void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);
    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();
    keyboard_state_activate();
    framebuffer_clear();

    // 1. Init Filesystem
    initialize_filesystem_ext2();
    framebuffer_write(0, 0, '1', GREEN, BLACK); // Tahap 1 OK

    // 2. TEST WRITE FOLDER "home"
    struct EXT2DriverRequest req;
    memset(&req, 0, sizeof(req));
    req.parent_inode = ROOT_INODE_NO;
    req.name = "home";
    req.name_len = 4;
    req.is_directory = true;
    req.buffer_size = 0; // Create folder
    
    int8_t ret = write(&req);
    if (ret == 0) framebuffer_write(1, 0, 'W', GREEN, BLACK); // Write Folder OK
    else framebuffer_write(1, 0, 'E', RED, BLACK);

    // 3. TEST WRITE FILE "iki" (isi: "halo")
    char file_content[] = "halo";
    req.name = "iki";
    req.name_len = 3;
    req.is_directory = false;
    req.buf = file_content;
    req.buffer_size = 4;

    ret = write(&req);
    if (ret == 0) framebuffer_write(2, 0, 'W', GREEN, BLACK); // Write File OK
    else framebuffer_write(2, 0, 'E', RED, BLACK);

    // 4. TEST READ DIRECTORY (Root)
    // Kita harap ada ".", "..", "home", "iki"
    char buffer_dir[1024]; // Buffer cukup besar
    memset(buffer_dir, 0, 1024);
    req.parent_inode = ROOT_INODE_NO; // Baca root
    req.buf = buffer_dir;
    req.buffer_size = 1024;

    ret = read_directory(&req);
    if (ret == 0) {
        framebuffer_write(3, 0, 'R', GREEN, BLACK); // Read Dir OK
        
        // Coba parse manual dan print huruf pertama tiap entry
        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry*) buffer_dir;
        int row = 4;
        int count = 0;
        while(count < 1024 && entry->inode != 0 && entry->rec_len != 0) {
            char *name = get_entry_name(entry);
            // Tulis nama file ke layar
            for (int i = 0; i < entry->name_len; i++) {
                framebuffer_write(row, i+2, name[i], WHITE, BLACK);
            }
            row++;
            entry = (struct EXT2DirectoryEntry*)((uint8_t*)entry + entry->rec_len);
            count += entry->rec_len;
        }
    } else {
        framebuffer_write(3, 0, 'E', RED, BLACK);
    }

    // 5. TEST READ FILE "iki"
    char buffer_read[10];
    memset(buffer_read, 0, 10);
    req.parent_inode = ROOT_INODE_NO;
    req.name = "iki";
    req.name_len = 3;
    req.buf = buffer_read;
    req.buffer_size = 5; // Cukup buat "halo"

    ret = read(req);
    if (ret == 0) {
        // Print isi file "halo"
        for(int i=0; i<4; i++) 
            framebuffer_write(10, i, buffer_read[i], YELLOW, BLACK);
    } else {
        framebuffer_write(10, 0, 'X', RED, BLACK);
    }

    // 6. TEST DELETE FILE "iki"
    req.parent_inode = ROOT_INODE_NO;
    req.name = "iki";
    req.name_len = 3;
    req.is_directory = false;
    
    ret = delete(req);
    if (ret == 0) framebuffer_write(12, 0, 'D', GREEN, BLACK); // Delete OK
    else framebuffer_write(12, 0, 'F', RED, BLACK);

    while(true);
}