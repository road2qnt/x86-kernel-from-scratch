#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

// Include header OS
#include "../header/filesystem/ext2.h"
#include "../header/driver/disk.h"

#ifndef BLOCK_SIZE
#define BLOCK_SIZE 512
#endif

#define STORAGE_SIZE (4 * 1024 * 1024)

uint8_t *image_storage = NULL;
// Kita definisikan ulang variabel ini khusus untuk konteks Inserter
// agar tidak konflik linking dengan ext2.c
bool g_filesystem_initialized_inserter = false;

// Override fungsi extern dari ext2.c jika diperlukan, 
// tapi biasanya ext2.c pakai variabel statis atau extern yang sama.
// Kita biarkan ext2.c mengelola g_filesystem_initialized-nya sendiri.

// --- MOCKING DRIVER ---
void read_blocks(void *ptr, uint32_t lba, uint8_t count) {
    if (!image_storage) { 
        fprintf(stderr, "[CRASH] Image storage NULL at Read LBA %u\n", lba); 
        exit(1); 
    }
    uint32_t offset = lba * BLOCK_SIZE;
    uint32_t size   = count * BLOCK_SIZE;
    if (offset + size > STORAGE_SIZE) {
        fprintf(stderr, "[CRASH] Read Out of Bounds! LBA: %u\n", lba);
        exit(1);
    }
    memcpy(ptr, image_storage + offset, size);
}

void write_blocks(const void *ptr, uint32_t lba, uint8_t count) {
    if (!image_storage) { 
        fprintf(stderr, "[CRASH] Image storage NULL at Write LBA %u\n", lba); 
        exit(1); 
    }
    uint32_t offset = lba * BLOCK_SIZE;
    uint32_t size   = count * BLOCK_SIZE;
    if (offset + size > STORAGE_SIZE) {
        fprintf(stderr, "[CRASH] Write Out of Bounds! LBA: %u\n", lba);
        exit(1);
    }
    memcpy(image_storage + offset, ptr, size);
}
// ---------------------------

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <src> <tgt> <inode> <disk>\n", argv[0]);
        exit(1);
    }

    char *source_path = argv[1];
    char *target_name = argv[2];
    int parent_inode = atoi(argv[3]);
    char *storage_path = argv[4];

    printf("[DEBUG] 1. Opening Storage: %s\n", storage_path);
    FILE *fptr = fopen(storage_path, "rb+");
    if (!fptr) {
        fptr = fopen(storage_path, "wb+");
        ftruncate(fileno(fptr), STORAGE_SIZE);
    }
    
    printf("[DEBUG] 2. Allocating RAM (%d bytes)\n", STORAGE_SIZE);
    image_storage = calloc(1, STORAGE_SIZE);
    if (!image_storage) return 1;

    printf("[DEBUG] 3. Reading Disk to RAM\n");
    fseek(fptr, 0, SEEK_SET);
    fread(image_storage, 1, STORAGE_SIZE, fptr);

    printf("[DEBUG] 4. Initializing Filesystem...\n");
    // Panggil fungsi init dari ext2.c
    initialize_filesystem_ext2();
    
    // HACK: Paksa variabel global di ext2.c jadi true
    // Kita perlu cara mengakses variabel 'g_filesystem_initialized' di ext2.c
    // Karena di C variabel global dishare, kita bisa set langsung jika extern.
    extern bool g_filesystem_initialized;
    g_filesystem_initialized = true;
    printf("[DEBUG] 5. Filesystem Initialized.\n");

    printf("[DEBUG] 6. Reading Source File: %s\n", source_path);
    FILE *fsrc = fopen(source_path, "rb");
    if (!fsrc) { perror("Src not found"); return 1; }
    fseek(fsrc, 0, SEEK_END);
    size_t fsize = ftell(fsrc);
    fseek(fsrc, 0, SEEK_SET);
    
    uint8_t *buf = malloc(fsize);
    fread(buf, 1, fsize, fsrc);
    fclose(fsrc);

    printf("[DEBUG] 7. Preparing Write Request (Size: %lu)\n", fsize);
    struct EXT2DriverRequest req;
    memset(&req, 0, sizeof(req));
    req.parent_inode = parent_inode;
    req.name = target_name;
    req.name_len = strlen(target_name);
    req.is_directory = false;
    req.buffer_size = fsize;
    req.buf = buf;

    printf("[DEBUG] 8. Executing write()...\n");
    int8_t ret = write(&req);
    
    if (ret == 1) {
        printf("[DEBUG] File exists, overwriting...\n");
        delete(req);
        ret = write(&req);
    }

    if (ret == 0) {
        printf("[DEBUG] 9. Success! Saving to Disk...\n");
        fseek(fptr, 0, SEEK_SET);
        fwrite(image_storage, 1, STORAGE_SIZE, fptr);
    } else {
        printf("[DEBUG] FAILED. Error code: %d\n", ret);
    }

    fclose(fptr);
    return 0;
}