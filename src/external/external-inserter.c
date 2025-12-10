#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "header/filesystem/ext2.h"
#include "header/driver/disk.h"

// Global variable
uint8_t *image_storage;
uint8_t *file_buffer;
uint8_t *read_buffer;

void read_blocks(void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    for (int i = 0; i < block_count; i++) {
        memcpy(
            (uint8_t*) ptr + BLOCK_SIZE*i, 
            image_storage + BLOCK_SIZE*(logical_block_address+i), 
            BLOCK_SIZE
        );
    }
}

void write_blocks(const void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    for (int i = 0; i < block_count; i++) {
        memcpy(
            image_storage + BLOCK_SIZE*(logical_block_address+i), 
            (uint8_t*) ptr + BLOCK_SIZE*i, 
            BLOCK_SIZE
        );
    }
}

// Helper: get filename length
static uint8_t get_filename_length(const char *name) {
    uint8_t len = 0;
    while (name[len] && len < 255) len++;
    return len;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: ./inserter <file_to_insert> <filename_in_fs> <parent_inode> <storage.bin>\n");
        fprintf(stderr, "Example: ./inserter bin/shell shell 2 bin/storage.bin\n");
        exit(1);
    }

    char *src_file = argv[1];      // File source di host (e.g., bin/shell)
    char *dest_name = argv[2];     // Nama file di filesystem (e.g., "shell")
    uint32_t parent_inode = 2;     // Default root
    char *storage_file = argv[4];  // Storage file

    if (argc >= 4) {
        sscanf(argv[3], "%u", &parent_inode);
    }
    if (argc >= 5) {
        storage_file = argv[4];
    } else {
        storage_file = argv[3]; // Backward compatibility
        parent_inode = 2;
        if (argc == 4) {
            sscanf(argv[2], "%u", &parent_inode);
            dest_name = argv[1];
            // Extract filename from path
            char *last_slash = strrchr(argv[1], '/');
            if (last_slash) dest_name = last_slash + 1;
        }
    }

    // Allocate memory
    image_storage = malloc(4*1024*1024);
    file_buffer   = malloc(4*1024*1024);
    read_buffer   = malloc(4*1024*1024);
    
    if (!image_storage || !file_buffer || !read_buffer) {
        fprintf(stderr, "Error: Failed to allocate memory\n");
        exit(1);
    }

    // Read storage into memory
    FILE *fptr = fopen(storage_file, "rb");
    if (!fptr) {
        fprintf(stderr, "Error: Cannot open storage file '%s'\n", storage_file);
        exit(1);
    }
    fread(image_storage, 4*1024*1024, 1, fptr);
    fclose(fptr);

    // Read target file
    FILE *fptr_target = fopen(src_file, "rb");
    size_t filesize = 0;
    if (fptr_target == NULL) {
        fprintf(stderr, "Error: Cannot open source file '%s'\n", src_file);
        exit(1);
    }
    fseek(fptr_target, 0, SEEK_END);
    filesize = ftell(fptr_target);
    fseek(fptr_target, 0, SEEK_SET);
    fread(file_buffer, filesize, 1, fptr_target);
    fclose(fptr_target);

    printf("Source file    : %s\n", src_file);
    printf("Dest filename  : %s\n", dest_name);
    printf("Parent inode   : %u\n", parent_inode);
    printf("Filesize       : %zu bytes\n", filesize);

    // Initialize EXT2
    initialize_filesystem_ext2();

    // Setup request
    uint8_t filename_length = get_filename_length(dest_name);
    
    struct EXT2DriverRequest request = {
        .buf = file_buffer,
        .buffer_size = filesize,
        .name = dest_name,
        .name_len = filename_length,
        .parent_inode = parent_inode,
        .is_directory = false
    };

    // Try to write
    int8_t retcode = write(&request);
    
    if (retcode == 1) {
        // File exists, try to delete and rewrite
        printf("File exists, replacing...\n");
        struct EXT2DriverRequest del_req = request;
        delete(del_req);
        retcode = write(&request);
    }
    
    if (retcode == 0) {
        printf("Write success!\n");
        
        // Verify by reading back
        memset(read_buffer, 0, 4*1024*1024);
        struct EXT2DriverRequest verify_req = request;
        verify_req.buf = read_buffer;
        int8_t read_ret = read(verify_req);
        if (read_ret == 0) {
            bool verified = true;
            for (size_t i = 0; i < filesize; i++) {
                if (read_buffer[i] != file_buffer[i]) {
                    verified = false;
                    break;
                }
            }
            printf("Verification: %s\n", verified ? "OK" : "FAILED");
        }
    } else if (retcode == 1) {
        printf("Error: File/folder name already exist\n");
    } else if (retcode == 2) {
        printf("Error: Invalid parent inode\n");
    } else {
        printf("Error: Unknown error (code %d)\n", retcode);
    }

    // Write image back to storage
    fptr = fopen(storage_file, "wb");
    if (!fptr) {
        fprintf(stderr, "Error: Cannot write to storage file\n");
        exit(1);
    }
    fwrite(image_storage, 4*1024*1024, 1, fptr);
    fclose(fptr);

    // Cleanup
    free(image_storage);
    free(file_buffer);
    free(read_buffer);

    return 0;
}
