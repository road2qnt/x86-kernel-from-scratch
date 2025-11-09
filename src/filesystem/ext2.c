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
static inline void clear_bit(uint8_t *bitmap, uint32_t idx) {
    bitmap[idx >> 3] &= (uint8_t)~(1u << (idx & 7u));
}

static uint32_t find_entry_in_dir(uint32_t dir_inode_num, const char *name, uint32_t name_len) {
    if (dir_inode_num == 0 || name == NULL || name_len == 0) return 0;

    struct EXT2Inode dir;
    if (!read_inode(dir_inode_num, &dir)) return 0;
    if ((dir.i_mode & EXT2_S_IFDIR) == 0) return 0;

    struct BlockBuffer blk;
    for (int bi = 0; bi < 12 && dir.i_block[bi] != 0; bi++) {
        read_blocks(&blk, dir.i_block[bi], 1);

        uint32_t off = (bi == 0) ? get_dir_first_child_offset(blk.buf) : 0;
        while (off < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *ent = get_directory_entry(blk.buf, off);
            if (ent->rec_len == 0) break;

            if (ent->inode != 0) {
                char *ename = get_entry_name(ent);
                if (ent->name_len == name_len && memcmp(ename, name, name_len) == 0) {
                    return ent->inode;  // ditemukan
                }
            }
            off += ent->rec_len;
        }
    }
    return 0; // tidak ditemukan
}

bool is_directory_empty(uint32_t inode_num) {
    if (inode_num == 0) return true;

    struct EXT2Inode dir;
    if (!read_inode(inode_num, &dir)) return true;
    if ((dir.i_mode & EXT2_S_IFDIR) == 0) return true;

    struct BlockBuffer blk;
    for (int bi = 0; bi < 12 && dir.i_block[bi] != 0; bi++) {
        read_blocks(&blk, dir.i_block[bi], 1);

        uint32_t off = (bi == 0) ? get_dir_first_child_offset(blk.buf) : 0;
        while (off < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *ent = get_directory_entry(blk.buf, off);
            if (ent->rec_len == 0) break;

            if (ent->inode != 0) {
                // Ada entry aktif → direktori TIDAK kosong
                return false;
            }
            off += ent->rec_len;
        }
    }
    // Tidak ada entry anak aktif
    return true;
}

void deallocate_node(uint32_t inode_num) {
    if (inode_num == 0) return;

    // 1) Baca inode target
    struct EXT2Inode node;
    if (!read_inode(inode_num, &node)) return;

    // 2) Bebaskan blok data (direct blocks saja sesuai alokasi saat ini)
    struct BlockBuffer block_bmap;
    read_blocks(&block_bmap, BLOCK_BITMAP_LBA, 1);

    for (int i = 0; i < 12; i++) {
        uint32_t lba = node.i_block[i];
        if (lba != 0) {
            // clear bit untuk block ini
            clear_bit(block_bmap.buf, lba);
            node.i_block[i] = 0;
        }
    }

    write_blocks(&block_bmap, BLOCK_BITMAP_LBA, 1);

    // 3) Kosongkan inode di inode bitmap
    struct BlockBuffer inode_bmap;
    read_blocks(&inode_bmap, INODE_BITMAP_LBA, 1);
    // inode numbering 1-based → bitmap index = inode_num - 1
    clear_bit(inode_bmap.buf, inode_num - 1);
    write_blocks(&inode_bmap, INODE_BITMAP_LBA, 1);

    // 4) Nol-kan inode di inode table (rapi)
    uint32_t bgd_idx = inode_to_bgd(inode_num);
    uint32_t local   = inode_to_local(inode_num);
    uint32_t ipb     = BLOCK_SIZE / sizeof(struct EXT2Inode);
    uint32_t blk_idx = local / ipb;
    uint32_t off_inb = (local % ipb) * sizeof(struct EXT2Inode);
    uint32_t inode_lba = _ext2_bgdt_state.table[bgd_idx].bg_inode_table + blk_idx;

    struct BlockBuffer iblk;
    read_blocks(&iblk, inode_lba, 1);
    struct EXT2Inode *slot = (struct EXT2Inode *)(iblk.buf + off_inb);
    memset(slot, 0, sizeof(struct EXT2Inode));
    write_blocks(&iblk, inode_lba, 1);
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


int8_t write(struct EXT2DriverRequest *request){
    // 1. Validasi Awal
    if (!g_filesystem_initialized || request == NULL) return -1;
    if (request->parent_inode == 0) return 2;               // Parent invalid
    if (request->name == NULL || request->name_len == 0) return -1;

    // 2. Baca inode parent dan pastikan bertipe direktori
    struct EXT2Inode parent_inode;
    if (!read_inode(request->parent_inode, &parent_inode)) return 2;
    if ((parent_inode.i_mode & EXT2_S_IFDIR) == 0) return 2;
    // Pastikan parent punya setidaknya satu data block
    if (parent_inode.i_block[0] == 0) return 2;

    // 3. Cek apakah nama sudah dipakai di direktori parent (jenis yang sama, jadi file dengan file, folder dengan folder)
    {
        struct EXT2Inode p; read_inode(request->parent_inode, &p);
        struct BlockBuffer dirbuf;
        bool same_type_exists = false;

        for (int bi = 0; bi < 12 && p.i_block[bi] != 0 && !same_type_exists; bi++) {
            read_blocks(&dirbuf, p.i_block[bi], 1);
            uint32_t off = (bi == 0) ? get_dir_first_child_offset(dirbuf.buf) : 0;
            while (off < BLOCK_SIZE) {
                struct EXT2DirectoryEntry *ent = get_directory_entry(dirbuf.buf, off);
                if (ent->rec_len == 0) break;
                if (ent->inode != 0 && ent->name_len == request->name_len &&
                    memcmp(get_entry_name(ent), request->name, request->name_len) == 0) {
                    bool ent_is_dir = (ent->file_type == EXT2_FT_DIR);
                    if (request->is_directory ? ent_is_dir
                                            : (ent->file_type == EXT2_FT_REG_FILE || ent->file_type == EXT2_FT_UNKNOWN)) {
                        same_type_exists = true; break;
                    }
                }
                off += ent->rec_len;
            }
        }
        if (same_type_exists) return 1;   // “file/folder already exist” untuk tipe yang sama saja
    }

    // 4. Alokasi inode baru
    struct BlockBuffer inode_bitmap_buf;
    read_blocks(&inode_bitmap_buf, INODE_BITMAP_LBA, 1);
    uint32_t new_inode_num = 0;
    // Lewati inode yang sudah terpakai (root + reserved)
    for (uint32_t i = INITIAL_USED_INODES; i < _ext2_superblock_state.s_inodes_count; i++) {
        uint8_t byte = inode_bitmap_buf.buf[i / 8];
        if ((byte & (1 << (i % 8))) == 0) {
            // Tandai bit inode sebagai terpakai
            inode_bitmap_buf.buf[i / 8] |= (1 << (i % 8));
            new_inode_num = i + 1; // inode dimulai dari 1
            break;
        }
    }
    if (new_inode_num == 0) return -1; // kehabisan inode
    // Update state free inode count
    _ext2_superblock_state.s_free_inodes_count--;
    _ext2_bgdt_state.table[0].bg_free_inodes_count--;

    // 5. Hitung jumlah block yang dibutuhkan
    uint32_t blocks_needed = 1;
    if (!request->is_directory) {
        blocks_needed = (request->buffer_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    }

    // 6. Alokasi block kosong
    struct BlockBuffer block_bitmap_buf;
    read_blocks(&block_bitmap_buf, BLOCK_BITMAP_LBA, 1);
    uint32_t allocated_blocks[12] = {0};
    uint32_t found_blocks = 0;
    for (uint32_t i = INITIAL_USED_BLOCKS; i < _ext2_superblock_state.s_blocks_count && found_blocks < blocks_needed; i++) {
        uint8_t b = block_bitmap_buf.buf[i / 8];
        if ((b & (1 << (i % 8))) == 0) {
            // Tandai block sebagai terpakai
            block_bitmap_buf.buf[i / 8] |= (1 << (i % 8));
            allocated_blocks[found_blocks++] = i;
        }
    }
    if (found_blocks < blocks_needed) return -1; // disk penuh

    // Update free block count
    _ext2_superblock_state.s_free_blocks_count -= found_blocks;
    _ext2_bgdt_state.table[0].bg_free_blocks_count -= found_blocks;
    if (request->is_directory) {
        _ext2_bgdt_state.table[0].bg_used_dirs_count++;
    }

    // 7. Siapkan inode baru
    struct EXT2Inode new_inode;
    memset(&new_inode, 0, sizeof(new_inode));
    if (request->is_directory) {
        new_inode.i_mode  = EXT2_S_IFDIR | 0755;
        new_inode.i_size  = BLOCK_SIZE;
        new_inode.i_links_count = 2; // "." dan ".."
        new_inode.i_blocks = BLOCK_SIZE / 512;
        new_inode.i_block[0] = allocated_blocks[0];
    } else {
        new_inode.i_mode  = EXT2_S_IFREG | 0644;
        new_inode.i_size  = request->buffer_size;
        new_inode.i_links_count = 1;
        new_inode.i_blocks = blocks_needed * (BLOCK_SIZE / 512);
        for (uint32_t i = 0; i < blocks_needed; i++) {
            new_inode.i_block[i] = allocated_blocks[i];
        }
    }

    // 8. Tulis isi file atau direktori ke block yang dialokasikan
    if (request->is_directory) {
        struct BlockBuffer dir_blk;
        memset(dir_blk.buf, 0, BLOCK_SIZE);
        struct EXT2DirectoryEntry *dot = (struct EXT2DirectoryEntry *)dir_blk.buf;
        // Entry "."
        dot->inode    = new_inode_num;
        dot->name_len = 1;
        dot->file_type = EXT2_FT_DIR;
        dot->rec_len = get_entry_record_len(1);
        memcpy(get_entry_name(dot), ".", 1);
        // Entry ".."
        struct EXT2DirectoryEntry *dotdot = get_next_directory_entry(dot);
        dotdot->inode    = request->parent_inode;
        dotdot->name_len = 2;
        dotdot->file_type = EXT2_FT_DIR;
        dotdot->rec_len = BLOCK_SIZE - dot->rec_len;
        memcpy(get_entry_name(dotdot), "..", 2);
        write_blocks(&dir_blk, allocated_blocks[0], 1);
    } else {
        for (uint32_t i = 0; i < blocks_needed; i++) {
            struct BlockBuffer file_blk;
            memset(file_blk.buf, 0, BLOCK_SIZE);
            uint32_t offset = i * BLOCK_SIZE;
            uint32_t copy_sz = request->buffer_size - offset;
            if (copy_sz > BLOCK_SIZE) copy_sz = BLOCK_SIZE;
            memcpy(file_blk.buf, (uint8_t *)request->buf + offset, copy_sz);
            write_blocks(&file_blk, allocated_blocks[i], 1);
        }
    }

    // 9. Tulis inode baru ke inode table
    uint32_t bgd_idx  = inode_to_bgd(new_inode_num);
    uint32_t local_idx = inode_to_local(new_inode_num);
    uint32_t inodes_per_block = BLOCK_SIZE / sizeof(struct EXT2Inode);
    uint32_t block_idx = local_idx / inodes_per_block;
    uint32_t inode_lba = _ext2_bgdt_state.table[bgd_idx].bg_inode_table + block_idx;
    uint32_t offset_in_block = (local_idx % inodes_per_block) * sizeof(struct EXT2Inode);

    struct BlockBuffer inode_buf;
    read_blocks(&inode_buf, inode_lba, 1);
    memcpy(inode_buf.buf + offset_in_block, &new_inode, sizeof(struct EXT2Inode));
    write_blocks(&inode_buf, inode_lba, 1);

    // 10. Sisipkan directory entry baru ke parent
    struct BlockBuffer parent_dir_blk;
    read_blocks(&parent_dir_blk, parent_inode.i_block[0], 1);
    uint32_t ins_off   = get_dir_first_child_offset(parent_dir_blk.buf);
    uint16_t new_rec_len = get_entry_record_len(request->name_len);
    struct EXT2DirectoryEntry *dentry;
    while (ins_off < BLOCK_SIZE) {
        dentry = get_directory_entry(parent_dir_blk.buf, ins_off);
        uint16_t ideal_len = get_entry_record_len(dentry->name_len);
        uint16_t leftover  = dentry->rec_len - ideal_len;
        if (leftover >= new_rec_len) {
            // perkecil entry sekarang
            dentry->rec_len = ideal_len;
            // buat entry baru setelahnya
            struct EXT2DirectoryEntry *new_ent = get_next_directory_entry(dentry);
            new_ent->inode     = new_inode_num;
            new_ent->name_len  = request->name_len;
            new_ent->file_type = request->is_directory ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
            new_ent->rec_len   = leftover;
            memcpy(get_entry_name(new_ent), request->name, request->name_len);
            write_blocks(&parent_dir_blk, parent_inode.i_block[0], 1);
            goto commit_metadata;
        }
        if (dentry->rec_len == 0) break;
        ins_off += dentry->rec_len;
    }

    // Jika ruang tidak cukup dalam block, alokasikan block baru untuk directory
    // Dapatkan index block kosong berikutnya untuk parent
    for (uint32_t i = 1; i < 12; i++) {
        if (parent_inode.i_block[i] == 0) {
            // ambil satu block kosong
            uint32_t new_dir_block = 0;
            // cari bit kosong baru dari bitmap
            for (uint32_t j = INITIAL_USED_BLOCKS; j < _ext2_superblock_state.s_blocks_count; j++) {
                uint8_t b = block_bitmap_buf.buf[j / 8];
                if ((b & (1 << (j % 8))) == 0) {
                    block_bitmap_buf.buf[j / 8] |= (1 << (j % 8));
                    new_dir_block = j;
                    _ext2_superblock_state.s_free_blocks_count--;
                    _ext2_bgdt_state.table[0].bg_free_blocks_count--;
                    break;
                }
            }
            if (new_dir_block == 0) return -1; // tidak ada block kosong

            // tulis entry baru di block baru
            struct BlockBuffer new_blk;
            memset(new_blk.buf, 0, BLOCK_SIZE);
            struct EXT2DirectoryEntry *new_ent = (struct EXT2DirectoryEntry *)new_blk.buf;
            new_ent->inode     = new_inode_num;
            new_ent->name_len  = request->name_len;
            new_ent->file_type = request->is_directory ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
            new_ent->rec_len   = BLOCK_SIZE;
            memcpy(get_entry_name(new_ent), request->name, request->name_len);
            write_blocks(&new_blk, new_dir_block, 1);

            // update parent inode pointer
            parent_inode.i_block[i] = new_dir_block;
            parent_inode.i_size  += BLOCK_SIZE;
            parent_inode.i_blocks += BLOCK_SIZE / 512;
            // simpan parent inode ke table
            // (gunakan perhitungan inode_lba/offset sama seperti di atas)
            uint32_t p_bgd_idx = inode_to_bgd(request->parent_inode);
            uint32_t p_local_idx = inode_to_local(request->parent_inode);
            uint32_t p_block_idx = p_local_idx / inodes_per_block;
            uint32_t p_inode_lba = _ext2_bgdt_state.table[p_bgd_idx].bg_inode_table + p_block_idx;
            uint32_t p_off_in_blk = (p_local_idx % inodes_per_block) * sizeof(struct EXT2Inode);
            struct BlockBuffer p_inode_buf;
            read_blocks(&p_inode_buf, p_inode_lba, 1);
            memcpy(p_inode_buf.buf + p_off_in_blk, &parent_inode, sizeof(struct EXT2Inode));
            write_blocks(&p_inode_buf, p_inode_lba, 1);
            break;
        }
    }
commit_metadata:
    // 11. Commit bitmap dan metadata ke disk
    write_blocks(&inode_bitmap_buf, INODE_BITMAP_LBA, 1);
    write_blocks(&block_bitmap_buf, BLOCK_BITMAP_LBA, 1);
    // Tulis superblock dan BGDT terbaru
    struct BlockBuffer sb_buf;
    memset(sb_buf.buf, 0, BLOCK_SIZE);
    memcpy(sb_buf.buf, &_ext2_superblock_state, sizeof(struct EXT2Superblock));
    write_blocks(&sb_buf, SUPERBLOCK_LBA, 1);
    struct BlockBuffer bgd_buf;
    memset(bgd_buf.buf, 0, BLOCK_SIZE);
    memcpy(bgd_buf.buf, &_ext2_bgdt_state.table[0], sizeof(struct EXT2BlockGroupDescriptor));
    write_blocks(&bgd_buf, BGDT_LBA, 1);
    return 0;
}

int8_t delete(struct EXT2DriverRequest request){
    // 0) Validasi awal
    if (!g_filesystem_initialized) return -1;
    if (request.parent_inode == 0) return 3;              // parent invalid
    if (!request.name || request.name_len == 0) return -1;

    // 1) Baca & validasi parent sebagai direktori
    struct EXT2Inode parent_inode;
    if (!read_inode(request.parent_inode, &parent_inode)) return 3;
    if ((parent_inode.i_mode & EXT2_S_IFDIR) == 0) return 3;

    // 2) Cari entry target di direktori parent secara eksplisit (scan blok direktori)
    //    (Kita cari yang namanya cocok *dan* tipe sesuai request.is_directory)
    uint32_t target_inode_num = 0;
    uint32_t target_block_lba = 0;
    uint32_t target_entry_off = 0;
    uint32_t prev_entry_off   = 0;   // offset entry sebelumnya (untuk merge rec_len)
    bool     has_prev         = false;

    // Scan 12 direct block parent (cukup untuk tugas IF2130)
    struct BlockBuffer dirbuf;
    for (int bi = 0; bi < 12 && parent_inode.i_block[bi] != 0 && target_inode_num == 0; bi++) {
        read_blocks(&dirbuf, parent_inode.i_block[bi], 1);

        // mulai dari setelah "." dan ".." jika ini blok pertama
        uint32_t off = (bi == 0) ? get_dir_first_child_offset(dirbuf.buf) : 0;
        has_prev = false;
        prev_entry_off = 0;

        while (off < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *ent = get_directory_entry(dirbuf.buf, off);
            if (ent->rec_len == 0) break;                  // guard
            if (ent->inode != 0) {                         // hanya entry aktif
                char *ename = get_entry_name(ent);
                bool name_match = (ent->name_len == request.name_len) &&
                                   (memcmp(ename, request.name, request.name_len) == 0);
                if (name_match) {
                    // Cocokkan tipe sesuai flag is_directory
                    bool is_dir_entry = (ent->file_type == EXT2_FT_DIR);
                    bool is_reg_entry = (ent->file_type == EXT2_FT_REG_FILE) || (ent->file_type == EXT2_FT_UNKNOWN);
                    if ((request.is_directory && is_dir_entry) ||
                        (!request.is_directory && is_reg_entry)) {
                        target_inode_num = ent->inode;
                        target_block_lba = parent_inode.i_block[bi];
                        target_entry_off = off;
                        break;
                    }
                }
            }
            // simpan prev hanya jika entry aktif (untuk merge saat hapus)
            if (ent->inode != 0) {
                has_prev = true;
                prev_entry_off = off;
            }
            off += ent->rec_len;
        }
        if (target_inode_num != 0) break;
    }

    if (target_inode_num == 0) return 1; // not found

    // 3) Baca inode target
    struct EXT2Inode target_inode;
    if (!read_inode(target_inode_num, &target_inode)) return 1;

    // 4) Validasi tipe sesuai permintaan
    if (request.is_directory) {
        if ((target_inode.i_mode & EXT2_S_IFDIR) == 0) return 1; // yang ada bukan dir
        // folder harus kosong (hanya "." dan "..")
        if (!is_directory_empty(target_inode_num)) return 2;     // folder not empty
    } else {
        // hapus file biasa
        if ((target_inode.i_mode & EXT2_S_IFREG) == 0) {
            // toleransi: banyak implementasi minimal tidak mengisi bit S_IFREG.
            // Jika directory -> salah tipe
            if (target_inode.i_mode & EXT2_S_IFDIR) return 1;
        }
    }

    // 5) Hapus entry di parent directory
    //    Strategi sederhana & valid: jika ada prev aktif → prev.rec_len += ent.rec_len (merge),
    //    else (entry pertama di blok/daftar anak) → tandai ent->inode = 0.
    {
        // (Re-read block supaya buffer segar untuk modifikasi)
        read_blocks(&dirbuf, target_block_lba, 1);

        struct EXT2DirectoryEntry *victim = get_directory_entry(dirbuf.buf, target_entry_off);

        if (has_prev && prev_entry_off != 0 && prev_entry_off < target_entry_off) {
            struct EXT2DirectoryEntry *prev = get_directory_entry(dirbuf.buf, prev_entry_off);
            // merge ruang victim ke prev
            prev->rec_len = prev->rec_len + victim->rec_len;
        } else {
            // tidak ada prev aktif — tandai kosong (inode=0) agar entri ini di-skip
            victim->inode = 0;
            // (opsional) boleh set file_type/name_len=0 untuk kebersihan
            // victim->file_type = EXT2_FT_UNKNOWN;
            // victim->name_len  = 0;
        }
        write_blocks(&dirbuf, target_block_lba, 1);
    }

    // 6) Hitung jumlah blok yang dibebaskan (untuk update counter),
    //    lalu deallocate inode+blocks via helper.
    //    i_blocks = jumlah unit 512-byte; blk = i_blocks / (BLOCK_SIZE/512)
    uint32_t blk_units = (BLOCK_SIZE / 512);
    uint32_t freed_blocks = target_inode.i_blocks / blk_units;

    deallocate_node(target_inode_num);

    // 7) Update penghitung free di superblock & BGDT (group 0, asumsi 1 group)
    _ext2_superblock_state.s_free_inodes_count++;
    _ext2_bgdt_state.table[0].bg_free_inodes_count++;

    if (request.is_directory) {
        if (_ext2_bgdt_state.table[0].bg_used_dirs_count > 0)
            _ext2_bgdt_state.table[0].bg_used_dirs_count--;
    }
    if (freed_blocks > 0) {
        _ext2_superblock_state.s_free_blocks_count += freed_blocks;
        _ext2_bgdt_state.table[0].bg_free_blocks_count += freed_blocks;
    }

    // 8) Commit metadata (SB & BGDT). Bitmap sudah ditangani oleh deallocate_node().
    struct BlockBuffer sb_buf; memset(sb_buf.buf, 0, BLOCK_SIZE);
    memcpy(sb_buf.buf, &_ext2_superblock_state, sizeof(struct EXT2Superblock));
    write_blocks(&sb_buf, SUPERBLOCK_LBA, 1);

    struct BlockBuffer bgd_buf; memset(bgd_buf.buf, 0, BLOCK_SIZE);
    memcpy(bgd_buf.buf, &_ext2_bgdt_state.table[0], sizeof(struct EXT2BlockGroupDescriptor));
    write_blocks(&bgd_buf, BGDT_LBA, 1);

    return 0;
}