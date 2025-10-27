#ifndef _EXT2_H
#define _EXT2_H

#include "header/driver/disk.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define SUPERBLOCK_LBA          1 // Atau 2 jika boot sector kepake 2 blok? Biasanya 1.
#define BGDT_LBA                (SUPERBLOCK_LBA + 1) // Biasanya 2
// Lokasi LBA di Group 0 (karena create_ext2 fokus ke group 0 dulu)
#define BLOCK_BITMAP_LBA        (BGDT_LBA + 1) // Biasanya 3
#define INODE_BITMAP_LBA        (BLOCK_BITMAP_LBA + 1) // Biasanya 4
#define INODE_TABLE_LBA         (INODE_BITMAP_LBA + 1) // Biasanya 5
// Lokasi Data Block Root (harus dihitung setelah tahu lokasi Inode Table)
#define ROOT_DIR_BLOCK_LBA      (INODE_TABLE_LBA + INODES_TABLE_BLOCK_COUNT) // Misal: 5 + 16 = 21

#define ROOT_INODE_NO           2 // Inode nomor 2 standar untuk root
#define EXT2_GOOD_OLD_FIRST_INO 11 // Inode 1-10 biasanya di-reserve

/* -- IF2130 File System constants -- */
#define BOOT_SECTOR 0 // legacy from FAT32 filesystem IF2130 OS
#define DISK_SPACE 4194304u // 4MB disk space (because our disk or storage.bin is 4MB)
#define EXT2_SUPER_MAGIC 0xEF53 // this indicating that the filesystem used by OS is ext2
#define INODE_SIZE sizeof(struct EXT2Inode) // size of inode
#define INODES_PER_TABLE (BLOCK_SIZE / INODE_SIZE) // number of inode per block (512 / )
#define GROUPS_COUNT (BLOCK_SIZE / sizeof(struct EXT2BlockGroupDescriptor)) / 2u // number of groups in the filesystem
#define BLOCKS_PER_GROUP (DISK_SPACE / BLOCK_SIZE / GROUPS_COUNT) // number of blocks per group
#define INODES_TABLE_BLOCK_COUNT 16u 
#define INODES_PER_GROUP (INODES_PER_TABLE * INODES_TABLE_BLOCK_COUNT) // number of inodes per group

#define TOTAL_BLOCKS (BLOCKS_PER_GROUP * GROUPS_COUNT) 
#define TOTAL_INODES (INODES_PER_GROUP * GROUPS_COUNT) 
// Hitungan untuk Group 0
#define INITIAL_USED_BLOCKS_IN_GROUP0 (1 + 1 + 1 + 1 + INODES_TABLE_BLOCK_COUNT + 1) // SB(di LBA1), BGDT(di LBA2), BBmap, IBmap, ITbl, RootData
#define INITIAL_USED_INODES_IN_GROUP0 EXT2_GOOD_OLD_FIRST_INO // Inode 1-11 kepake (termasuk root #2)
// Total (jika cuma 1 group)
#define INITIAL_USED_BLOCKS INITIAL_USED_BLOCKS_IN_GROUP0
#define INITIAL_USED_INODES INITIAL_USED_INODES_IN_GROUP0

/**
 * inodes constant 
 * - reference: https://www.nongnu.org/ext2-doc/ext2.html#inode-table
 */
#define EXT2_S_IFREG 0x8000 // regular file 
#define EXT2_S_IFDIR 0x4000 // directory


/* FILE TYPE CONSTANT*/
/**
 * reference: 
 * - https://www.nongnu.org/ext2-doc/ext2.html#linked-directories
 * - Table 4.2. Defined Inode File Type Values
 */

#define EXT2_FT_UNKNOWN 0 // Unknown File Type
#define EXT2_FT_REG_FILE 1 // Regular File
#define EXT2_FT_DIR 2 // Directory
#define EXT2_FT_NEXT 3 // Character Special File

/**
 * EXT2DriverRequest
 * Derived dand modified from FAT32DriverRequest legacy IF2130 OS 
 */
struct EXT2DriverRequest
{
    void *buf; 
    char *name; 
    uint8_t name_len; 
    uint32_t parent_inode; 
    uint32_t buffer_size; 

    bool is_directory; 
}__attribute__((packed));

/**
 * EXT2Superblock: 
 * - https://www.nongnu.org/ext2-doc/ext2.html#superblock
 */
struct EXT2Superblock
{
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    // --- TAMBAHKAN INI ---
    uint32_t s_log_block_size;    // Log2 of block size (0=1KB, 1=2KB, 2=4KB)
    uint32_t s_log_frag_size;     // Log2 of fragment size (biasanya sama)
    // ---------------------
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;             // Mount time
    uint32_t s_wtime;             // Write time
    uint16_t s_mnt_count;         // Mount count
    uint16_t s_max_mnt_count;     // Max mount count
    uint16_t s_magic;             // Magic signature (0xEF53)
    uint16_t s_state;             // Filesystem state
    uint16_t s_errors;            // Behaviour when detecting errors
    uint16_t s_minor_rev_level;   // Minor revision level
    uint32_t s_lastcheck;         // Time of last check
    uint32_t s_checkinterval;     // Max time between checks
    uint32_t s_creator_os;        // OS
    uint32_t s_rev_level;         // Revision level
    uint16_t s_def_resuid;        // Default uid for reserved blocks
    uint16_t s_def_resgid;        // Default gid for reserved blocks
    // --- EXT2_DYNAMIC_REV Specific (penting kalau rev_level >= 1) ---
    uint32_t s_first_ino;         // First non-reserved inode (biasanya 11)
    uint16_t s_inode_size;        // Size of inode structure (biasanya 128)
    uint16_t s_block_group_nr;    // Block group # of this superblock
    // ... (feature flags, uuid, volume name, dll. bisa ditambah kalau perlu) ...
    // Pastikan total ukuran struct ini pas (biasanya 1024 byte), tambah padding jika perlu.
    uint8_t padding[1024 - 104]; // Sesuaikan ukuran padding ini! Hitung ukuran field di atas dulu.

}__attribute__((packed));


/**
 * reference: 
 * - https://www.nongnu.org/ext2-doc/ext2.html#block-group-descriptor-table
 */
struct EXT2BlockGroupDescriptor
{
    /**
     * 32bit block id of the first block of the “block bitmap” for the group represented.
     * The actual block bitmap is located within its own allocated blocks starting at the block ID specified by this value.    
     */
    uint32_t bg_block_bitmap; 

    /**
     * 32bit block id of the first block of the “inode bitmap” for the group represented.
     */
    uint32_t bg_inode_bitmap;

    /**
     * 16bit value indicating the total number of free blocks for the represented group.
     */
    uint32_t bg_inode_table;

    uint16_t bg_free_blocks_count;

    /**
     * 16bit value indicating the total number of free inodes for the represented group.
     */
    uint16_t bg_free_inodes_count;

    /**
     * 16bit value indicating the number of inodes allocated to directories for the represented group.
     */
    uint16_t bg_used_dirs_count;

    /**
     * 16bit value used for padding the structure on a 32bit boundary.
     */
    uint16_t bg_pad;

    /**
     * 12 bytes of reserved space for future revisions.
     */
    uint32_t bg_reserved[3]; // 12 bytes of reserved space for future revisions. 
}__attribute__((packed));

/**
 * reference: 
 * - https://www.nongnu.org/ext2-doc/ext2.html#block-group-descriptor-table
 */
struct EXT2BlockGroupDescriptorTable
{
    struct EXT2BlockGroupDescriptor table[GROUPS_COUNT]; // can be change with fixed size array
};


/**
 * EXT2Inode
 * Inode stands for index node, it is a data structure in a Unix-style file system that describes a file-system object such as a file or a directory.
 */

struct EXT2Inode
{
    uint16_t i_mode;        // File mode
    uint16_t i_uid;         // User id
    uint32_t i_size;        // Size in bytes
    uint32_t i_atime;       // Access time
    uint32_t i_ctime;       // Creation time
    uint32_t i_mtime;       // Modification time
    uint32_t i_dtime;       // Deletion time
    uint16_t i_gid;         // Group id
    uint16_t i_links_count; // Gabungan
    uint32_t i_blocks;      // Blocks count (in 512b units)
    uint32_t i_flags;       // File flags
    uint32_t i_osd1;        // OS dependent 1
    uint32_t i_block[15];   // Block map
    uint32_t i_generation;  // File version (for NFS)
    uint32_t i_file_acl;    // File ACL
    uint32_t i_dir_acl;     // Directory ACL / high 32 bits of file size
    uint32_t i_faddr;       // Fragment address
    uint8_t  i_osd2[12];    // OS dependent 2
}__attribute__((packed));

struct EXT2InodeTable
{
    struct EXT2Inode table[INODES_PER_GROUP]; // can be change with fixed size array
};

/**
 * EXT2DirectoryEntry
 * Linked List Directory
 * reference: 
 * - https://www.nongnu.org/ext2-doc/ext2.html#linked-directories
 */

struct EXT2DirectoryEntry
{
    uint32_t inode; // 32bit value indicating the inode number of the file entry. A value of 0 indicate that the entry is not used.
    /**
     * 16bit unsigned displacement to the next directory entry from the start of the current directory entry.
     * This field must have a value at least equal to the length of the current record.
     * The directory entries must be aligned on 4 bytes boundaries and there cannot be any directory entry spanning multiple data blocks.
     * If an entry cannot completely fit in one block, it must be pushed to the next data block and the rec_len of the previous entry properly adjusted.
     */
    uint16_t rec_len; 

    /**
     * 8bit value indicating the length of the file name.
     */
    uint16_t name_len;

    /**
     * 8bit unsigned value used to indicate file type.
     */
    uint8_t file_type;

}__attribute__((packed));

/**
 *  REGULAR function
 */

/**
 * get the name of the entry
 * @param entry the directory entry
 * @return the name of the entry
 */
char *get_entry_name(void *entry);

/**
 * get the directory entry from the buffer
 * @param ptr the buffer that contains the directory table
 * @param offset the offset of the entry 
 * @return the directory entry
 */
struct EXT2DirectoryEntry *get_directory_entry(void *ptr, uint32_t offset);

/**
 * get the next directory entry from the current entry
 * @param entry the current entry
 * @return the next directory entry
 */
struct EXT2DirectoryEntry *get_next_directory_entry(struct EXT2DirectoryEntry *entry);

/**
 * get the record length of the entry
 * @param name_len the length of the name of the entry
 * @return the record length of the entry
 */
uint16_t get_entry_record_len(uint8_t name_len);

/**
 * get the offset of the first child of the directory
 * @param ptr the buffer that contains the directory table
 * @return the offset of the first child of the directory
 */
uint32_t get_dir_first_child_offset(void *ptr);


/* =================== MAIN FUNCTION OF EXT32 FILESYSTEM ============================*/

/**
 * @brief get bgd index from inode, inode will starts at index 1
 * @param inode 1 to INODES_PER_GROUP * GROUP_COUNT
 * @return bgd index (0 to GROUP_COUNT - 1)
 */
uint32_t inode_to_bgd(uint32_t inode);

/**
 * @brief get inode local index in the corrresponding bgd
 * @param inode 1 to INODES_PER_GROUP * GROUP_COUNT
 * @return local index
 */
uint32_t inode_to_local(uint32_t inode);

/**
 * @brief create a new directory using given node
 * first item of directory table is its node location (name will be .)
 * second item of directory is its parent location (name will be ..)
 * @param node pointer of inode
 * @param inode inode that already allocated
 * @param parent_inode inode of parent directory (if root directory, the parent is itself)
 */
void init_directory_table(struct EXT2Inode *node, uint32_t inode, uint32_t parent_inode);
/**
 * @brief check whether filesystem signature is missing or not in boot sector
 *
 * @return true if memcmp(boot_sector, fs_signature) returning inequality
 */
bool is_empty_storage(void);

/**
 * @brief create a new EXT2 filesystem. Will write fs_signature into boot sector,
 * initialize super block, bgd table, block and inode bitmap, and create root directory
 */
void create_ext2(void);

/**
 * @brief Initialize file system driver state, if is_empty_storage() then create_ext2()
 * Else, read and cache super block (located at block 1) and bgd table (located at block 2) into state
 */
void initialize_filesystem_ext2(void);

/**
 * @brief check whether a directory table has children or not
 * @param inode of a directory table
 * @return true if first_child_entry->inode = 0
 */
bool is_directory_empty(uint32_t inode);



/* =============================== CRUD FUNC ======================================== */

/**
 * @brief EXT2 Folder / Directory read
 * @param request buf point to struct EXT2 Directory
 * @return Error code: 0 success - 1 not a folder - 2 not found - 3 parent folder invalid - -1 unknown
 */
int8_t read_directory(struct EXT2DriverRequest *prequest);

/**
 * @brief EXT2 read, read a file from file system
 * @param request All attribute will be used except is_dir for read, buffer_size will limit reading count
 * @return Error code: 0 success - 1 not a file - 2 not enough buffer - 3 not found - 4 parent folder invalid - -1 unknown
 */
int8_t read(struct EXT2DriverRequest request);

/**
 * @brief EXT2 write, write a file or a folder to file system
 *
 * @param All attribute will be used for write except is_dir, buffer_size == 0 then create a folder / directory. It is possible that exist file with name same as a folder
 * @return Error code: 0 success - 1 file/folder already exist - 2 invalid parent folder - -1 unknown
 */
int8_t write(struct EXT2DriverRequest *request);

/**
 * @brief EXT2 delete, delete a file or empty directory in file system
 *  @param request buf and buffer_size is unused, is_dir == true means delete folder (possible file with name same as folder)
 * @return Error code: 0 success - 1 not found - 2 folder is not empty - 3 parent folder invalid -1 unknown
 */
int8_t delete(struct EXT2DriverRequest request);

/* =============================== MEMORY ==========================================*/

/**
 * @brief get a free inode from the disk, assuming it is always
 * available
 * @return new inode
 */
uint32_t allocate_node(void); 

/**
 * @brief deallocate node from the disk, will also deallocate its used blocks
 * also all of the blocks of indirect blocks if necessary
 * @param inode that needs to be deallocated
 */
void deallocate_node(uint32_t inode);

/**
 * @brief deallocate node blocks
 * @param locations node->block
 * @param blocks number of blocks
 */
void deallocate_blocks(void *loc, uint32_t blocks);

/**
 * @brief deallocate block from the disk
 * @param locations block locations
 * @param blocks number of blocks
 * @param bitmap block bitmap
 * @param depth depth of the block
 * @param last_bgd last bgd that is used
 * @param bgd_loaded whether bgd is loaded or not
 * @return new last bgd
 */
uint32_t deallocate_block(uint32_t *locations, uint32_t blocks, struct BlockBuffer *bitmap, uint32_t depth, uint32_t *last_bgd, bool bgd_loaded);

/**
 * @brief write node->block in the given node, will allocate
 * at least node->blocks number of blocks, if first 12 item of node-> block
 * is not enough, will use indirect blocks
 * @param ptr the buffer that needs to be written
 * @param node pointer of the node
 * @param preffered_bgd it is located at the node inode bgd
 * 
 * @attention only implement until doubly indirect block, if you want to implement triply indirect block please increase the storage size to at least 256MB
 */
void allocate_node_blocks(void *ptr, struct EXT2Inode *node, uint32_t prefered_bgd);

/**
 * @brief update the node to the disk
 * @param node pointer of node
 * @param inode location of the node
 */
void sync_node(struct EXT2Inode *node, uint32_t inode);

#endif
