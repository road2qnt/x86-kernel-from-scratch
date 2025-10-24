#include "../header/filesystem/ext2.h"
#include "../header/driver/disk.h"
#include "../header/stdlib/string.h"
#include "../header/stdlib/boolean.h"

// Global state variables for the EXT2 filesystem
static struct EXT2Superblock _ext2_superblock_state;
static struct EXT2BlockGroupDescriptorTable _ext2_bgdt_state;

const uint8_t fs_signature[BLOCK_SIZE] = {
    'C', 'o', 'u', 'r', 's', 'e', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',  ' ',
    'D', 'e', 's', 'i', 'g', 'n', 'e', 'd', ' ', 'b', 'y', ' ', ' ', ' ', ' ',  ' ',
    'L', 'a', 'b', ' ', 'S', 'i', 's', 't', 'e', 'r', ' ', 'I', 'T', 'B', ' ',  ' ',
    'M', 'a', 'd', 'e', ' ', 'w', 'i', 't', 'h', ' ', '<', '3', ' ', ' ', ' ',  ' ',
    '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '2', '0', '2', '5', '\n',
    [BLOCK_SIZE-2] = 'O',
    [BLOCK_SIZE-1] = 'k',
};

// Fungsi-fungsi REGULAR

// Fungsi ini buat dapetin nama dari sebuah entri direktori.
// Nama filenya itu ada setelah bagian data utama dari struct EXT2DirectoryEntry.
char *get_entry_name(void *entry) {
    return (char *)((uint8_t *)entry + sizeof(struct EXT2DirectoryEntry));
}

// Fungsi ini buat dapetin entri direktori dari sebuah buffer (tempat data direktori).
// Kita kasih offset biar bisa langsung lompat ke entri yang diinginkan.
struct EXT2DirectoryEntry *get_directory_entry(void *ptr, uint32_t offset) {
    return (struct EXT2DirectoryEntry *)((uint8_t *)ptr + offset);
}

// Fungsi ini buat dapetin entri direktori berikutnya dari entri yang sekarang.
// Dia pakai rec_len (panjang record) buat tahu di mana entri selanjutnya dimulai.
struct EXT2DirectoryEntry *get_next_directory_entry(struct EXT2DirectoryEntry *entry) {
    if (entry->rec_len == 0) {
        return NULL; // Kalo rec_len-nya 0, berarti udah nyampe akhir atau ada yang aneh.
    }
    return (struct EXT2DirectoryEntry *)((uint8_t *)entry + entry->rec_len);
}

// Fungsi ini buat ngitung panjang record (rec_len) dari sebuah entri direktori.
// Panjang record itu harus kelipatan 4 byte dan cukup buat nyimpen data entri + nama filenya.
uint16_t get_entry_record_len(uint8_t name_len) {
    uint16_t base_len = sizeof(struct EXT2DirectoryEntry) + name_len; 
    return (base_len + 3) & ~3; // Ini buat nge-align ke kelipatan 4 byte.
}

// Fungsi ini buat dapetin offset (jarak) ke entri anak pertama di sebuah direktori.
// Biasanya, dua entri pertama itu "." (direktori itu sendiri) dan ".." (direktori induk).
// Jadi, kita lewatin dua entri itu buat nyari anak yang beneran.
uint32_t get_dir_first_child_offset(void *ptr) {
    struct EXT2DirectoryEntry *dot_entry = get_directory_entry(ptr, 0);
    if (dot_entry == NULL) return 0; 

    struct EXT2DirectoryEntry *dot_dot_entry = get_next_directory_entry(dot_entry);
    if (dot_dot_entry == NULL) return 0; 

    return (uint32_t)((uint8_t *)dot_dot_entry + dot_dot_entry->rec_len - (uint8_t *)ptr);
}


/* =================== MAIN FUNCTION OF EXT32 FILESYSTEM ============================*/

uint32_t inode_to_bgd(uint32_t inode) {
    // Fungsi ini buat nyari tahu inode ini ada di grup blok deskriptor yang ke berapa.
    // Inode itu dimulai dari 1, jadi kita kurangin 1 dulu.
    return (inode - 1) / INODES_PER_GROUP;
}

uint32_t inode_to_local(uint32_t inode) {
    // Fungsi ini buat nyari tahu indeks lokal inode di dalam grup bloknya.
    // Jadi, di grup blok itu, inode ini urutan ke berapa.
    return (inode - 1) % INODES_PER_GROUP;
}

void init_directory_table(struct EXT2Inode *node, uint32_t inode, uint32_t parent_inode) {
    // Fungsi ini buat inisialisasi tabel direktori baru.
    // Setiap direktori itu pasti punya dua entri awal: "." (dirinya sendiri) dan ".." (direktori induknya).
    // Kita bikin entri-entri ini di buffer dulu, nanti baru ditulis ke disk.
    // ribet belum gua kerjain wkwkw
    struct EXT2DirectoryEntry dot_entry;
    dot_entry.inode = inode;
    dot_entry.name_len = 1;
    dot_entry.file_type = EXT2_FT_DIR;
    dot_entry.rec_len = get_entry_record_len(dot_entry.name_len);

    struct EXT2DirectoryEntry dot_dot_entry;
    dot_dot_entry.inode = parent_inode;
    dot_dot_entry.name_len = 2;
    dot_dot_entry.file_type = EXT2_FT_DIR;
    dot_dot_entry.rec_len = get_entry_record_len(dot_dot_entry.name_len);

    uint8_t dir_block_buffer[BLOCK_SIZE];
    memset(dir_block_buffer, 0, BLOCK_SIZE);

    struct EXT2DirectoryEntry *current_entry = (struct EXT2DirectoryEntry *)dir_block_buffer;
    memcpy(current_entry, &dot_entry, sizeof(struct EXT2DirectoryEntry));
    memcpy(get_entry_name(current_entry), ".", dot_entry.name_len);

    current_entry = (struct EXT2DirectoryEntry *)((uint8_t *)current_entry + dot_entry.rec_len);
    memcpy(current_entry, &dot_dot_entry, sizeof(struct EXT2DirectoryEntry));
    memcpy(get_entry_name(current_entry), "..", dot_dot_entry.name_len);
}

bool is_directory_empty(uint32_t inode) {
    // ribet belum gua kerjain wkwkw
    // This is a minimal implementation. A proper implementation would read the directory's data block
    // and iterate through its entries to check if any valid entries exist beyond '.' and '..'.
    // For now, we'll assume it's empty if the inode is 0 (which is not strictly correct for a directory).
    // A more accurate check would involve reading the directory's data block and checking the third entry.
    return true;
}

bool is_empty_storage(void) {
    // Fungsi ini buat ngecek apakah disk kita masih kosong atau udah ada filesystem EXT2-nya.
    // Caranya, kita baca boot sector (blok paling awal) terus bandingin sama signature EXT2 yang kita punya.
    // Kalo beda, berarti kosong atau belum diformat EXT2.
    struct BlockBuffer boot_sector_buffer;
    read_blocks(&boot_sector_buffer, BOOT_SECTOR, 1);
    return memcmp(boot_sector_buffer.buf, fs_signature, BLOCK_SIZE) != 0;
}

void create_ext2(void) {
    // Fungsi ini buat bikin filesystem EXT2 baru di disk.
    // Pertama, kita tulis signature EXT2 di boot sector.
    // Terus, kita inisialisasi superblock dan tabel deskriptor grup blok.
    // ribet belum gua kerjain wkwkw
    struct BlockBuffer boot_sector_buffer;
    memcpy(boot_sector_buffer.buf, fs_signature, BLOCK_SIZE);
    write_blocks(&boot_sector_buffer, BOOT_SECTOR, 1);

    struct EXT2Superblock superblock;
    memset(&superblock, 0, sizeof(struct EXT2Superblock));
    superblock.s_magic = EXT2_SUPER_MAGIC;
    write_blocks((void*)&superblock, 1, 1);

    struct EXT2BlockGroupDescriptorTable bgdt;
    memset(&bgdt, 0, sizeof(struct EXT2BlockGroupDescriptorTable));
    write_blocks((void*)&bgdt, 2, 1);
}

void initialize_filesystem_ext2(void) {
    // Fungsi ini buat inisialisasi driver filesystem EXT2.
    // Dia ngecek dulu disk-nya kosong apa enggak.
    // Kalo kosong, dia panggil create_ext2() buat bikin filesystem baru.
    // Kalo udah ada, dia baca superblock sama tabel deskriptor grup blok ke memori.
    // ribet belum gua kerjain wkwkw
    if (is_empty_storage()) {
        create_ext2();
    } else {
        struct BlockBuffer superblock_buffer;
        read_blocks(&superblock_buffer, 1, 1);

        struct BlockBuffer bgdt_buffer;
        read_blocks(&bgdt_buffer, 2, 1);
    }
}

bool is_directory_empty(uint32_t inode) {
    // Fungsi ini buat ngecek apakah sebuah direktori itu kosong (gak punya file/subdirektori lain selain "." dan "..").
    // ribet belum gua kerjain wkwkw
    return true;
}



/* =============================== CRUD FUNC ========================================
 */

int8_t read_directory(struct EXT2DriverRequest *prequest) {
    // Fungsi ini buat baca isi sebuah direktori.
    // Dia bakal ngisi buffer yang dikasih sama request dengan daftar entri di direktori itu.
    // ribet belum gua kerjain wkwkw
    return -1;
}

int8_t read(struct EXT2DriverRequest request) {
    // Fungsi ini buat baca isi file dari filesystem.
    // Dia bakal baca data dari file yang diminta ke dalam buffer yang disediain di request.
    // ribet belum gua kerjain wkwkw
    return -1;
}

int8_t write(struct EXT2DriverRequest *request) {
    // Fungsi ini buat nulis file atau bikin folder baru di filesystem.
    // Kalo buffer_size-nya 0, berarti dia bikin folder.
    // ribet belum gua kerjain wkwkw
    return -1;
}

int8_t delete(struct EXT2DriverRequest request) {
    // Fungsi ini buat ngehapus file atau folder kosong dari filesystem.
    // ribet belum gua kerjain wkwkw
    return -1;
}

/* =============================== MEMORY ==========================================
 */

uint32_t allocate_node(void) {
    // Fungsi ini buat nyari inode yang kosong (belum dipakai) di disk.
    // Kalo udah ketemu, dia bakal tandain inode itu sebagai udah dipakai terus balikin nomornya.
    // ribet belum gua kerjain wkwkw
    return 0;
}

void deallocate_node(uint32_t inode) {
    // Fungsi ini buat ngebebasin inode yang udah gak dipakai lagi.
    // Dia juga bakal ngebebasin blok-blok data yang dipakai sama inode ini.
    // ribet belum gua kerjain wkwkw
}

void deallocate_blocks(void *loc, uint32_t blocks) {
    // Fungsi ini buat ngebebasin blok-blok data di disk.
    // Dia bakal tandain blok-blok ini sebagai kosong di bitmap blok.
    // ribet belum gua kerjain wkwkw
}

uint32_t deallocate_block(uint32_t *locations, uint32_t blocks, struct BlockBuffer *bitmap, uint32_t depth, uint32_t *last_bgd, bool bgd_loaded) {
    // Fungsi ini buat ngebebasin blok data secara lebih detail, termasuk blok-blok tidak langsung (indirect blocks).
    // Ini lumayan kompleks karena harus ngurusin struktur data blok yang berlapis-lapis.
    // ribet belum gua kerjain wkwkw
    return 0;
}

void allocate_node_blocks(void *ptr, struct EXT2Inode *node, uint32_t prefered_bgd) {
    // Fungsi ini buat ngalokasiin blok-blok data buat sebuah inode.
    // Kalo blok langsung (direct blocks) gak cukup, dia bakal pakai blok tidak langsung (indirect blocks).
    // ribet belum gua kerjain wkwkw
}

void sync_node(struct EXT2Inode *node, uint32_t inode) {
    // Fungsi ini buat nyimpen perubahan dari sebuah inode ke disk.
    // Jadi, kalo ada perubahan di inode (misalnya ukuran file berubah), dia bakal nulis balik ke disk.
    // ribet belum gua kerjain wkwkw
}

