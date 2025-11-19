#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h> 
#include "../header/filesystem/ext2.h"
#include "../header/driver/disk.h"

#ifndef BLOCK_SIZE
#define BLOCK_SIZE 512
#endif

uint8_t *image_storage;

// --- MOCKING DRIVER DISK ---
// Fungsi ini menggantikan disk.c kernel. 
// Alih-alih baca port I/O, dia baca/tulis array RAM 'image_storage'.

void read_blocks(void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    // Simulasi baca dari disk (copy dari image_storage ke buffer tujuan)
    memcpy(ptr, image_storage + (logical_block_address * BLOCK_SIZE), block_count * BLOCK_SIZE);
}

void write_blocks(const void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    // Simulasi tulis ke disk (copy dari buffer sumber ke image_storage)
    memcpy(image_storage + (logical_block_address * BLOCK_SIZE), ptr, block_count * BLOCK_SIZE);
}
// ---------------------------

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <source_file> <target_filename> <parent_inode> <storage_path>\n", argv[0]);
        fprintf(stderr, "Example: ./inserter bin/shell shell 2 bin/storage.bin\n");
        exit(1);
    }

    char *source_path = argv[1];
    char *target_name = argv[2];
    int parent_inode = atoi(argv[3]);
    char *storage_path = argv[4];

    // 1. Buka dan Baca Storage.bin ke RAM
    FILE *fptr_storage = fopen(storage_path, "rb+");
    if (!fptr_storage) {
        perror("Failed to open storage file");
        return 1;
    }

    // Alokasi memori 4MB untuk disk image
    image_storage = malloc(4 * 1024 * 1024);
    fread(image_storage, 4 * 1024 * 1024, 1, fptr_storage);

    // 2. Baca Source File (Program User yang mau dimasukkan)
    FILE *fptr_source = fopen(source_path, "rb");
    if (!fptr_source) {
        perror("Failed to open source file");
        return 1;
    }

    // Hitung ukuran file source
    fseek(fptr_source, 0, SEEK_END);
    size_t filesize = ftell(fptr_source);
    fseek(fptr_source, 0, SEEK_SET);

    // Baca file source ke buffer
    uint8_t *file_buffer = malloc(filesize);
    fread(file_buffer, filesize, 1, fptr_source);
    fclose(fptr_source);

    printf("Inserting: %s -> %s (Size: %lu bytes) into Inode %d\n", 
            source_path, target_name, filesize, parent_inode);

    // 3. Inisialisasi Filesystem State
    // Ini akan membaca Superblock & BGDT dari image_storage (karena read_blocks kita mock)
    initialize_filesystem_ext2();

    // 4. Siapkan Request Write
    struct EXT2DriverRequest request;
    memset(&request, 0, sizeof(struct EXT2DriverRequest));
    request.parent_inode = parent_inode;
    request.name = target_name;
    request.name_len = strlen(target_name);
    request.is_directory = false;
    request.buffer_size = filesize;
    request.buf = file_buffer;

    // 5. Eksekusi Write
    int8_t ret = write(&request);

    // Handle jika file sudah ada (Overwrite logic)
    if (ret == 1) {
        printf("File exists. Deleting and rewriting...\n");
        // Hapus file lama
        int8_t del_ret = delete(request); // struct di-pass by value sesuai definisi baru lo
        if (del_ret != 0) {
            printf("Failed to delete existing file. Error: %d\n", del_ret);
            return 1;
        }
        // Coba tulis lagi
        ret = write(&request);
    }

    if (ret == 0) {
        printf("Success inserting file!\n");
    } else {
        printf("Failed to insert file. Error code: %d\n", ret);
        return 1;
    }

    // 6. Simpan Perubahan ke Disk Fisik
    // Kembalikan posisi pointer file storage ke awal
    fseek(fptr_storage, 0, SEEK_SET);
    // Tulis seluruh isi RAM image_storage balik ke file storage.bin
    fwrite(image_storage, 4 * 1024 * 1024, 1, fptr_storage);
    
    fclose(fptr_storage);
    free(image_storage);
    free(file_buffer);

    return 0;
}