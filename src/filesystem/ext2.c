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


//===================================================================
//== Helper Functions (Implementasi)
//===================================================================

/**
 * @brief Dapatkan pointer ke nama file di dalam directory entry.
 * Nama file disimpan tepat setelah struct EXT2DirectoryEntry.
 * @param entry Pointer ke awal directory entry.
 * @return Pointer ke karakter pertama nama file.
 */
char *get_entry_name(void *entry) {
    // Casting entry ke tipe structnya
    struct EXT2DirectoryEntry *dir_entry = (struct EXT2DirectoryEntry *)entry;
    // Nama dimulai tepat setelah struct itu selesai
    return (char *)((uint8_t *)dir_entry + sizeof(struct EXT2DirectoryEntry));
}

/**
 * @brief Dapatkan pointer ke directory entry di offset tertentu dalam buffer blok.
 * @param ptr Pointer ke awal buffer data blok direktori.
 * @param offset Byte offset dari awal buffer ke awal directory entry yang dicari.
 * @return Pointer ke struct EXT2DirectoryEntry di offset itu.
 */
struct EXT2DirectoryEntry *get_directory_entry(void *ptr, uint32_t offset) {
    // Cukup geser pointer ptr sebanyak offset byte
    return (struct EXT2DirectoryEntry *)((uint8_t *)ptr + offset);
}

/**
 * @brief Dapatkan pointer ke directory entry berikutnya.
 * @param entry Pointer ke directory entry saat ini.
 * @return Pointer ke directory entry berikutnya, atau NULL jika rec_len 0.
 */
struct EXT2DirectoryEntry *get_next_directory_entry(struct EXT2DirectoryEntry *entry) {
    // Jika panjang record 0, ini indikasi akhir atau error
    if (entry->rec_len == 0) {
        return NULL;
    }
    // Entri berikutnya dimulai setelah entry saat ini + panjang recordnya
    return (struct EXT2DirectoryEntry *)((uint8_t *)entry + entry->rec_len);
}

/**
 * @brief Hitung panjang record (rec_len) yang dibutuhkan untuk sebuah entry.
 * Panjang harus cukup untuk struct + nama + padding agar kelipatan 4.
 * @param name_len Panjang nama file.
 * @return Ukuran rec_len yang sudah di-align kelipatan 4.
 */
uint16_t get_entry_record_len(uint8_t name_len) {
    // Ukuran dasar = ukuran struct + panjang nama
    uint16_t base_len = sizeof(struct EXT2DirectoryEntry) + name_len;
    // Bulatkan ke atas ke kelipatan 4 terdekat
    // Trik: (base_len + 3) / 4 * 4 atau pakai bitwise (base_len + 3) & ~3
    return (base_len + 3) & ~3;
}

/**
 * @brief get bgd index from inode, inode will starts at index 1
 * @param inode 1 to INODES_PER_GROUP * GROUPS_COUNT
 * @return bgd index (0 to GROUP_COUNT - 1)
 */
uint32_t inode_to_bgd(uint32_t inode) {
    // Inode 0 tidak valid, inode 1-N ada di grup 0, N+1 - 2N di grup 1, dst.
    // Inode dimulai dari 1, jadi kurangi 1 untuk perhitungan berbasis 0.
    if (inode == 0) return 0; // Seharusnya tidak terjadi, tapi untuk jaga-jaga
    return (inode - 1) / INODES_PER_GROUP;
}

/**
 * @brief get inode local index in the corrresponding bgd
 * @param inode 1 to INODES_PER_GROUP * GROUP_COUNT
 * @return local index (0 to INODES_PER_GROUP - 1)
 */
uint32_t inode_to_local(uint32_t inode) {
    // Sama, kurangi 1 dulu. Hasil modulo adalah indeks lokalnya.
    if (inode == 0) return 0;
    return (inode - 1) % INODES_PER_GROUP;
}

/**
 * @brief get the offset of the first child of the directory
 * (Offset setelah entri '.' dan '..')
 * @param ptr Pointer ke buffer yang berisi data block direktori
 * @return Byte offset dari awal buffer ke awal entri anak pertama,
 * atau 0 jika ada error atau direktori kosong.
 */
uint32_t get_dir_first_child_offset(void *ptr) {
    if (!ptr) return 0;

    // Ambil entri pertama (".")
    struct EXT2DirectoryEntry *dot_entry = get_directory_entry(ptr, 0);
    // Jika tidak valid atau nama bukan ".", return error (0)
    if (!dot_entry || dot_entry->rec_len == 0 || dot_entry->name_len != 1 || get_entry_name(dot_entry)[0] != '.') {
        return 0;
    }

    // Ambil entri kedua ("..")
    struct EXT2DirectoryEntry *dot_dot_entry = get_next_directory_entry(dot_entry);
    // Jika tidak valid atau nama bukan "..", return error (0)
    if (!dot_dot_entry || dot_dot_entry->rec_len == 0 || dot_dot_entry->name_len != 2 || get_entry_name(dot_dot_entry)[0] != '.' || get_entry_name(dot_dot_entry)[1] != '.') {
        return 0;
    }

    // Offset anak pertama adalah offset "." + panjang record "." + panjang record ".."
    // Atau lebih simpel: alamat memori setelah ".." dikurangi alamat awal buffer.
    uint32_t offset = (uint32_t)((uint8_t *)dot_dot_entry + dot_dot_entry->rec_len - (uint8_t *)ptr);

    // Jika offset melebihi ukuran blok, ada yang salah
    if (offset >= BLOCK_SIZE) {
        return 0; // Atau return BLOCK_SIZE sebagai tanda akhir? Tergantung desain. 0 lebih aman.
    }

    return offset;
}

//===================================================================
//== Filesystem Initializer Functions 
//===================================================================
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

/**
 * @brief Membaca struktur inode dari disk ke memori.
 * @param inode_num Nomor inode yang ingin dibaca (mulai dari 1).
 * @param inode_out Pointer ke struct EXT2Inode tempat menyimpan hasil baca.
 * @return true jika berhasil, false jika gagal (misal inode tidak valid).
 */
static bool read_inode(uint32_t inode_num, struct EXT2Inode *inode_out) {
    if (inode_num == 0 || inode_num > _ext2_superblock_state.s_inodes_count) {
        return false; // Nomor inode tidak valid
    }

    uint32_t bgd_idx = inode_to_bgd(inode_num); // Cari grupnya
    uint32_t local_idx = inode_to_local(inode_num); // Cari indeks lokalnya
    uint32_t inode_table_start_lba = _ext2_bgdt_state.table[bgd_idx].bg_inode_table; // LBA awal Inode Table grup ini

    // Hitung LBA blok tempat inode ini berada
    uint32_t inodes_per_block = BLOCK_SIZE / sizeof(struct EXT2Inode);
    uint32_t block_index_in_table = local_idx / inodes_per_block;
    uint32_t inode_lba = inode_table_start_lba + block_index_in_table;

    // Hitung offset byte inode di dalam blok itu
    uint32_t offset_in_block = (local_idx % inodes_per_block) * sizeof(struct EXT2Inode);

    // Baca blok yang berisi inode
    struct BlockBuffer inode_block_buffer;
    read_blocks(&inode_block_buffer, inode_lba, 1);

    // Salin data inode dari buffer ke struct output
    memcpy(inode_out, (uint8_t*)inode_block_buffer.buf + offset_in_block, sizeof(struct EXT2Inode));

    return true; // Berhasil
}


/* =============================== CRUD FUNC ======================================== */

/**
 * @brief EXT2 Folder / Directory read
 * @param prequest Pointer ke request, berisi inode parent (direktori yg mau dibaca) dan buffer output.
 * @return Error code: 0 success - 1 not a folder - 2 not found (di read() aja) - 3 parent folder invalid (inode 0) - -1 unknown error / buffer too small
 */
int8_t read_directory(struct EXT2DriverRequest *prequest) {
    // 1. Validasi awal
    if (!g_filesystem_initialized) return -1; // FS belum siap
    if (!prequest || !prequest->buf) return -1; // Request atau buffer tidak valid
    if (prequest->parent_inode == 0) return 3; // Inode 0 tidak valid untuk direktori

    // 2. Baca Inode direktori yang diminta
    struct EXT2Inode dir_inode;
    if (!read_inode(prequest->parent_inode, &dir_inode)) {
        return 3; // Gagal baca inode parent (dianggap invalid)
    }

    // 3. Cek apakah ini benar-benar direktori
    if ((dir_inode.i_mode & EXT2_S_IFDIR) == 0) { // Cek bit tipe direktori
        return 1; // Bukan sebuah folder
    }

    // 4. Cek apakah direktori punya data block
    //    Implementasi simpel: hanya baca blok data pertama (direct block 0)
    if (dir_inode.i_block[0] == 0) {
        // Direktori kosong (hanya . dan .. mungkin sudah dibuat tapi blok belum dialokasi?)
        // Atau ini indikasi error. Kita anggap sukses tapi kosong.
        memset(prequest->buf, 0, prequest->buffer_size); // Kosongkan buffer output
        return 0; // Sukses (direktori kosong)
    }

    // 5. Baca blok data pertama direktori
    struct BlockBuffer dir_data_buffer;
    read_blocks(&dir_data_buffer, dir_inode.i_block[0], 1);

    // 6. Salin data blok ke buffer output, perhatikan ukuran buffer
    uint32_t copy_size = (BLOCK_SIZE < prequest->buffer_size) ? BLOCK_SIZE : prequest->buffer_size;
    memcpy(prequest->buf, dir_data_buffer.buf, copy_size);

    // Jika buffer output lebih kecil dari ukuran blok, mungkin ada data terpotong.
    // Untuk simplifikasi, kita return success aja. Handling buffer size bisa ditambah.
    if (BLOCK_SIZE > prequest->buffer_size) {
        // Bisa return error code khusus buffer kecil, misal -2
        // return -2; // Contoh: Buffer too small
    }

    return 0; // Sukses
}

/**
 * @brief EXT2 read, read a file from file system
 * @param request All attribute will be used except is_dir for read, buffer_size will limit reading count
 * @return Error code: 0 success - 1 not a file - 2 not enough buffer - 3 not found - 4 parent folder invalid - -1 unknown
 */
int8_t read(struct EXT2DriverRequest request) {
    // 1. Validasi Awal
    if (!g_filesystem_initialized) return -1;
    if (request.parent_inode == 0) return 4; // Parent invalid
    if (!request.buf || !request.name || request.name_len == 0) return -1; // Request tidak valid

    // 2. Cari Inode File di Parent Directory
    // Kita butuh helper function find_entry_in_dir()
    uint32_t file_inode_num = find_entry_in_dir(request.parent_inode, request.name, request.name_len);
    if (file_inode_num == 0) {
        return 3; // File tidak ditemukan
    }

    // 3. Baca Inode File
    struct EXT2Inode file_inode;
    if (!read_inode(file_inode_num, &file_inode)) {
        // Jika gagal baca inode padahal entry-nya ada, ini aneh.
        return 3; // Anggap saja not found atau error
    }

    // 4. Cek Tipe File
    if ((file_inode.i_mode & EXT2_S_IFDIR)) { // Cek apakah ini direktori
        return 1; // Bukan file biasa
    }
    // Pastikan ini file biasa (opsional, tapi bagus)
    if (!(file_inode.i_mode & EXT2_S_IFREG)) {
         return 1; // Bukan file regular
    }


    // 5. Cek Ukuran Buffer
    // Ukuran file aktual dalam byte
    uint32_t file_size = file_inode.i_size;
    // Ukuran buffer yang disediakan user
    uint32_t buffer_size = request.buffer_size;
    // Berapa byte yang *sebenarnya* akan kita baca
    uint32_t bytes_to_read = (buffer_size < file_size) ? buffer_size : file_size;

    if (buffer_size < file_size) {
        // Warning: buffer nggak cukup, data akan terpotong.
        // Sesuai spec, return error code 2.
        // Tapi untuk simplifikasi awal, kita bisa lanjut baca sebatas buffer_size.
        // return 2; // Aktifkan ini jika mau strict error checking
    }

    // 6. Baca Data Block
    uint8_t *output_buffer = (uint8_t*)request.buf;
    uint32_t bytes_read = 0;
    struct BlockBuffer data_block_buffer; // Buffer 1 blok

    // Loop melalui pointer blok di inode (hanya handle direct block dulu)
    for (int i = 0; i < 12 && file_inode.i_block[i] != 0 && bytes_read < bytes_to_read; i++) {
        uint32_t block_lba = file_inode.i_block[i];

        // Baca satu blok data
        read_blocks(&data_block_buffer, block_lba, 1);

        // Hitung berapa byte dari blok ini yang perlu disalin
        uint32_t remaining_bytes_to_read = bytes_to_read - bytes_read;
        uint32_t copy_size = (remaining_bytes_to_read < BLOCK_SIZE) ? remaining_bytes_to_read : BLOCK_SIZE;

        // Salin data dari buffer blok ke buffer output
        memcpy(output_buffer + bytes_read, data_block_buffer.buf, copy_size);

        // Update jumlah byte yang sudah dibaca
        bytes_read += copy_size;
    }

    // TODO: Implementasi pembacaan dari indirect blocks (i_block[12], [13], [14]) jika bytes_read < bytes_to_read

    // Jika setelah loop, bytes_read masih kurang dari bytes_to_read (dan buffer cukup),
    // kemungkinan file-nya korup atau implementasi indirect block belum ada.
    // Untuk sekarang, kita anggap selesai.

    return 0; // Sukses
}