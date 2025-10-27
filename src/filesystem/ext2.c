#include "header/filesystem/ext2.h" 
#include "header/driver/disk.h"   
#include "header/stdlib/string.h"  
#include "header/stdlib/boolean.h" 
#include "header/stdlib/utils.h"   

static struct EXT2Superblock _ext2_superblock_state;
static struct EXT2BlockGroupDescriptorTable _ext2_bgdt_state;
static bool g_filesystem_initialized = false;

const uint8_t fs_signature[BLOCK_SIZE] = { 
    'R', 'E', 'N', 'D', 'A', 'N', 'G', 'O', 'S', '_', 'F', 'S', '_', 'V', '1', '.',
    [BLOCK_SIZE-2] = 'O', 
    [BLOCK_SIZE-1] = 'k',
};

/**
 * @brief Cek apakah disk masih kosong (belum diformat EXT2).
 * @return true jika kosong, false jika sudah ada signature.
 */
bool is_empty_storage(void) {
    struct BlockBuffer boot_sector_buffer; 
    read_blocks(&boot_sector_buffer, BOOT_SECTOR, 1); 
    return memcmp(boot_sector_buffer.buf, fs_signature, BLOCK_SIZE) != 0; 
}

/**
 * @brief Membuat (memformat) filesystem EXT2 baru di disk.
 */
void create_ext2(void) {
    // Buffer serbaguna (ukuran BLOCK_SIZE, misal 1KB)
    // Kita pakai array biar cukup besar untuk nulis inode table nanti
    struct BlockBuffer buffer[INODES_TABLE_BLOCK_COUNT + 1];

    // --- 1. Tulis Signature ---
    memset(buffer[0].buf, 0, BLOCK_SIZE); // Bersihkan buffer dulu
    memcpy(buffer[0].buf, fs_signature, sizeof(fs_signature)); // Lebih aman pakai sizeof
    write_blocks(&buffer[0], BOOT_SECTOR, 1); // Tulis ke LBA 0

    // --- 2. Siapkan & Tulis Superblock ---
    memset(&_ext2_superblock_state, 0, sizeof(struct EXT2Superblock));
    // Isi field Superblock berdasarkan konstanta di ext2.h
    _ext2_superblock_state.s_inodes_count      = TOTAL_INODES; //
    _ext2_superblock_state.s_blocks_count      = TOTAL_BLOCKS; //
    _ext2_superblock_state.s_free_blocks_count = TOTAL_BLOCKS - INITIAL_USED_BLOCKS; //
    _ext2_superblock_state.s_free_inodes_count = TOTAL_INODES - INITIAL_USED_INODES; //
    _ext2_superblock_state.s_first_data_block  = BGDT_LBA + 1;   _ext2_superblock_state.s_first_data_block  = (BLOCK_SIZE == 1024) ? 1 : 0; // SB di blok 1
    _ext2_superblock_state.s_log_block_size    = (BLOCK_SIZE <= 1024) ? 0 : ((BLOCK_SIZE == 2048) ? 1 : 2); // 0=1K, 1=2K, 2=4K
    _ext2_superblock_state.s_inodes_per_group  = INODES_PER_GROUP; //
    _ext2_superblock_state.s_blocks_per_group  = BLOCKS_PER_GROUP; //
    _ext2_superblock_state.s_magic             = EXT2_SUPER_MAGIC; // 0xEF53
    _ext2_superblock_state.s_first_ino         = EXT2_GOOD_OLD_FIRST_INO; // Inode #11
    _ext2_superblock_state.s_inode_size        = sizeof(struct EXT2Inode); //

    memset(buffer[0].buf, 0, BLOCK_SIZE); // Pastikan sisa buffer nol
    memcpy(buffer[0].buf, &_ext2_superblock_state, sizeof(struct EXT2Superblock));
    write_blocks(&buffer[0], SUPERBLOCK_LBA, 1); // Tulis ke LBA 1

    // --- 3. Siapkan & Tulis Block Group Descriptor Table (BGDT) ---
    memset(&_ext2_bgdt_state, 0, sizeof(struct EXT2BlockGroupDescriptorTable));
    // Asumsi hanya 1 group (GROUPS_COUNT = 1)
    _ext2_bgdt_state.table[0].bg_block_bitmap      = BLOCK_BITMAP_LBA; //
    _ext2_bgdt_state.table[0].bg_inode_bitmap      = INODE_BITMAP_LBA; //
    _ext2_bgdt_state.table[0].bg_inode_table       = INODE_TABLE_LBA;  //
    _ext2_bgdt_state.table[0].bg_free_blocks_count = BLOCKS_PER_GROUP - INITIAL_USED_BLOCKS; //
    _ext2_bgdt_state.table[0].bg_free_inodes_count = INODES_PER_GROUP - INITIAL_USED_INODES; //
    _ext2_bgdt_state.table[0].bg_used_dirs_count   = 1; // Untuk root directory

    memset(buffer[0].buf, 0, BLOCK_SIZE);
    memcpy(buffer[0].buf, &_ext2_bgdt_state.table[0], sizeof(struct EXT2BlockGroupDescriptor));
    write_blocks(&buffer[0], BGDT_LBA, 1); // Tulis ke LBA 2

    // --- 4. Siapkan & Tulis Bitmaps ---
    // Block Bitmap
    memset(buffer[0].buf, 0, BLOCK_SIZE); // 0 = free
    for (uint32_t i = 0; i < INITIAL_USED_BLOCKS; ++i) { //
        set_bit(buffer[0].buf, i); // Tandai blok awal sebagai terpakai
    }
    write_blocks(&buffer[0], BLOCK_BITMAP_LBA, 1); //

    // Inode Bitmap
    memset(buffer[0].buf, 0, BLOCK_SIZE); // 0 = free
    for (uint32_t i = 1; i <= INITIAL_USED_INODES; ++i) { //
         set_bit(buffer[0].buf, i - 1); // Tandai inode awal (1-11) sebagai terpakai (index bitmap 0-10)
    }
    write_blocks(&buffer[0], INODE_BITMAP_LBA, 1); //


    // --- 5. Siapkan & Tulis Inode Table (Kosong kecuali Root Inode) ---
    // Gunakan buffer array yang sudah disiapkan
    memset(buffer, 0, sizeof(buffer)); // Bersihkan semua buffer inode table

    // Dapatkan pointer ke lokasi inode root (#2) di buffer
    // Hitung di blok ke berapa & offset ke berapa inode #2 berada
    uint32_t root_inode_local_index = ROOT_INODE_NO - 1; // Index 1
    uint32_t inodes_per_block = BLOCK_SIZE / sizeof(struct EXT2Inode); //
    uint32_t root_inode_block_index_in_table = root_inode_local_index / inodes_per_block; // Blok ke-0 dari table
    uint32_t root_inode_offset_in_block = root_inode_local_index % inodes_per_block; // Offset di dalam blok

    struct EXT2Inode *root_inode = (struct EXT2Inode*) (buffer[root_inode_block_index_in_table].buf + root_inode_offset_in_block * sizeof(struct EXT2Inode));

    // Isi inode root
    root_inode->i_mode  = EXT2_S_IFDIR | 0755; // Direktori, izin rwxr-xr-x
    root_inode->i_size  = BLOCK_SIZE;          // Ukuran awal direktori = 1 blok
    root_inode->i_links_count = 2;             // Link dari "." dan ".."
    root_inode->i_blocks = BLOCK_SIZE / 512;    // Jumlah blok 512-byte
    root_inode->i_block[0] = ROOT_DIR_BLOCK_LBA; // Lokasi data block root (dari ext2.h)

    // Tulis seluruh Inode Table (sebanyak INODES_TABLE_BLOCK_COUNT blok) ke Disk
    write_blocks(buffer, INODE_TABLE_LBA, INODES_TABLE_BLOCK_COUNT); //

    // --- 6. Siapkan & Tulis Data Block Root ---
    memset(buffer[0].buf, 0, BLOCK_SIZE);
    struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)buffer[0].buf;

    // Entri "."
    entry->inode = ROOT_INODE_NO; // Inode root
    entry->rec_len = get_entry_record_len(1);
    entry->name_len = 1;
    entry->file_type = EXT2_FT_DIR; //
    memcpy(get_entry_name(entry), ".", 1);

    // Entri ".."
    uint16_t current_rec_len = entry->rec_len; // Simpan panjang "."
    entry = get_next_directory_entry(entry); // Geser pointer
    entry->inode = ROOT_INODE_NO; // Parent root adalah root
    entry->rec_len = BLOCK_SIZE - current_rec_len; // Isi sisa blok
    entry->name_len = 2;
    entry->file_type = EXT2_FT_DIR; //
    memcpy(get_entry_name(entry), "..", 2);

    write_blocks(&buffer[0], ROOT_DIR_BLOCK_LBA, 1); //

    g_filesystem_initialized = true; // Tandai FS sudah siap
}

/**
 * @brief Initialize file system driver state, if is_empty_storage() then create_ext2()
 * Else, read and cache super block (located at block 1) and bgd table (located at block 2) into state
 */
void initialize_filesystem_ext2(void) {
    // 1. Cek dulu disk-nya kosong apa enggak pakai is_empty_storage()
    if (is_empty_storage()) {
        // 2a. Kalau kosong, panggil create_ext2() buat bikin filesystem baru
        create_ext2();
        // Setelah create_ext2(), state Superblock & BGDT udah ada di variabel global
        // Tandai filesystem udah siap
        g_filesystem_initialized = true;
    } else {
        // 2b. Kalau udah ada isinya, baca struktur data yang udah ada dari disk
        // Baca Superblock (dari LBA 1) ke variabel global _ext2_superblock_state
        read_blocks(&_ext2_superblock_state, SUPERBLOCK_LBA, 1);
        // Baca Block Group Descriptor Table (dari LBA 2) ke variabel global _ext2_bgdt_state
        // Asumsi cuma 1 block group, jadi baca BGD pertama aja
        read_blocks(&_ext2_bgdt_state.table[0], BGDT_LBA, 1);

        // Opsional tapi bagus: Pastikan magic number-nya bener (0xEF53)
        if (_ext2_superblock_state.s_magic == EXT2_SUPER_MAGIC) {
            // Kalau magic number cocok, tandai filesystem udah siap
            g_filesystem_initialized = true;
        } else {
            // Kalau nggak cocok, berarti ada error, filesystem nggak valid
            g_filesystem_initialized = false;
            // Lo bisa tambahin pesan error ke framebuffer di sini kalau mau
        }
    }
}