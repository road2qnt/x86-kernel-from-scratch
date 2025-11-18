#include "../header/filesystem/ext2.h"
#include "../header/driver/disk.h"
#include "../header/stdlib/string.h"
#include "../header/stdlib/boolean.h"
#include "../header/stdlib/utils.h"

// ==============================================================================
// GLOBAL STATE & CONSTANTS
// ==============================================================================

static struct EXT2Superblock _ext2_superblock_state;
static struct EXT2BlockGroupDescriptorTable _ext2_bgdt_state;
static bool g_filesystem_initialized = false;

const uint8_t fs_signature[BLOCK_SIZE] = {
    'R', 'E', 'N', 'D', 'A', 'N', 'G', 'O', 'S', ' ', 'F', 'S', ' ', 'v', '1', '.',
    [BLOCK_SIZE-2] = 'O', [BLOCK_SIZE-1] = 'k',
};

// ==============================================================================
// FORWARD DECLARATIONS (PROTOTYPES)
// ==============================================================================

static bool read_inode(uint32_t inode_num, struct EXT2Inode *inode_out);
static void write_inode(uint32_t inode_num, struct EXT2Inode *inode_in);
static uint32_t find_entry_in_dir(uint32_t dir_inode_num, const char *name, uint32_t name_len);
static int8_t add_entry_to_dir(uint32_t parent_inode, uint32_t new_inode_num, char *name, uint8_t name_len, uint8_t file_type);
static int8_t remove_entry_from_dir(uint32_t parent_inode, char *name_to_remove, uint8_t name_len_to_remove);
static uint32_t allocate_block(uint32_t preferred_bgd_index);
static void deallocate_single_block(uint32_t block_num);

// ==============================================================================
// HELPER FUNCTIONS (DATA STRUCTURES)
// ==============================================================================

char *get_entry_name(void *entry) {
    struct EXT2DirectoryEntry *e = (struct EXT2DirectoryEntry *)entry;
    return (char *)(e + 1);
}

struct EXT2DirectoryEntry *get_directory_entry(void *ptr, uint32_t offset) {
    return (struct EXT2DirectoryEntry *)((uint8_t *)ptr + offset);
}

struct EXT2DirectoryEntry *get_next_directory_entry(struct EXT2DirectoryEntry *entry) {
    if (entry->rec_len == 0) return NULL;
    return (struct EXT2DirectoryEntry *)((uint8_t *)entry + entry->rec_len);
}

uint16_t get_entry_record_len(uint8_t name_len) {
    uint16_t len = sizeof(struct EXT2DirectoryEntry) + name_len;
    return (len + 3) & ~3;
}

uint32_t get_dir_first_child_offset(void *ptr) {
    struct EXT2DirectoryEntry *dot = get_directory_entry(ptr, 0);
    if (!dot || dot->rec_len == 0) return 0;
    struct EXT2DirectoryEntry *dotdot = get_next_directory_entry(dot);
    if (!dotdot || dotdot->rec_len == 0) return 0;
    return (uint32_t)((uint8_t *)dotdot + dotdot->rec_len - (uint8_t *)ptr);
}

uint32_t inode_to_bgd(uint32_t inode) {
    if (inode == 0) return 0;
    return (inode - 1) / INODES_PER_GROUP;
}

uint32_t inode_to_local(uint32_t inode) {
    if (inode == 0) return 0;
    return (inode - 1) % INODES_PER_GROUP;
}

// ==============================================================================
// HELPER FUNCTIONS (DISK I/O ABSTRACTION)
// ==============================================================================

static bool read_inode(uint32_t inode_num, struct EXT2Inode *inode_out) {
    if (inode_num == 0) return false;
    
    uint32_t bgd_idx = inode_to_bgd(inode_num);
    uint32_t local_idx = inode_to_local(inode_num);
    
    uint32_t inodes_per_block = BLOCK_SIZE / sizeof(struct EXT2Inode);
    uint32_t block_idx = local_idx / inodes_per_block;
    uint32_t offset = (local_idx % inodes_per_block) * sizeof(struct EXT2Inode);
    
    uint32_t table_lba = _ext2_bgdt_state.table[bgd_idx].bg_inode_table;
    
    struct BlockBuffer buf;
    read_blocks(&buf, table_lba + block_idx, 1);
    memcpy(inode_out, buf.buf + offset, sizeof(struct EXT2Inode));
    return true;
}

static void write_inode(uint32_t inode_num, struct EXT2Inode *inode_in) {
    if (inode_num == 0) return;

    uint32_t bgd_idx = inode_to_bgd(inode_num);
    uint32_t local_idx = inode_to_local(inode_num);

    uint32_t inodes_per_block = BLOCK_SIZE / sizeof(struct EXT2Inode);
    uint32_t block_idx = local_idx / inodes_per_block;
    uint32_t offset = (local_idx % inodes_per_block) * sizeof(struct EXT2Inode);

    uint32_t table_lba = _ext2_bgdt_state.table[bgd_idx].bg_inode_table;

    struct BlockBuffer buf;
    read_blocks(&buf, table_lba + block_idx, 1);
    memcpy(buf.buf + offset, inode_in, sizeof(struct EXT2Inode));
    write_blocks(&buf, table_lba + block_idx, 1);
}

static uint32_t find_entry_in_dir(uint32_t dir_inode_num, const char *name, uint32_t name_len) {
    struct EXT2Inode dir_inode;
    if (!read_inode(dir_inode_num, &dir_inode)) return 0;
    if ((dir_inode.i_mode & EXT2_S_IFDIR) == 0) return 0;

    struct BlockBuffer buf;
    for (int i = 0; i < 12; i++) {
        if (dir_inode.i_block[i] == 0) break;
        
        read_blocks(&buf, dir_inode.i_block[i], 1);
        
        uint32_t offset = 0;
        while (offset < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *ent = get_directory_entry(buf.buf, offset);
            if (ent->rec_len == 0) break; 
            
            if (ent->inode != 0) {
                char *ent_name = get_entry_name(ent);
                if (ent->name_len == name_len && memcmp(ent_name, name, name_len) == 0) {
                    return ent->inode; 
                }
            }
            offset += ent->rec_len;
        }
    }
    return 0; 
}

static uint32_t allocate_block(uint32_t preferred_bgd_index) {
    (void)preferred_bgd_index; 
    uint32_t bgd_idx = 0; // Always group 0 for simplicity

    if (_ext2_bgdt_state.table[bgd_idx].bg_free_blocks_count == 0) return 0; 

    struct BlockBuffer bitmap;
    read_blocks(&bitmap, BLOCK_BITMAP_LBA, 1); 

    for (uint32_t i = 0; i < BLOCKS_PER_GROUP; i++) {
        if (test_bit(bitmap.buf, i) == 0) { 
            set_bit(bitmap.buf, i); 
            write_blocks(&bitmap, BLOCK_BITMAP_LBA, 1);

            _ext2_superblock_state.s_free_blocks_count--;
            _ext2_bgdt_state.table[bgd_idx].bg_free_blocks_count--;

            struct BlockBuffer meta_buf;
            memcpy(meta_buf.buf, &_ext2_superblock_state, sizeof(struct EXT2Superblock));
            write_blocks(&meta_buf, SUPERBLOCK_LBA, 1);
            
            memcpy(meta_buf.buf, &_ext2_bgdt_state.table[0], sizeof(struct EXT2BlockGroupDescriptor));
            write_blocks(&meta_buf, BGDT_LBA, 1);

            return i; 
        }
    }
    return 0; 
}

static void deallocate_single_block(uint32_t block_num) {
    if (block_num == 0) return;
    
    struct BlockBuffer bitmap;
    read_blocks(&bitmap, BLOCK_BITMAP_LBA, 1); 
    
    if (test_bit(bitmap.buf, block_num)) {
        clear_bit(bitmap.buf, block_num);
        write_blocks(&bitmap, BLOCK_BITMAP_LBA, 1);

        _ext2_superblock_state.s_free_blocks_count++;
        _ext2_bgdt_state.table[0].bg_free_blocks_count++;

        struct BlockBuffer meta_buf;
        memcpy(meta_buf.buf, &_ext2_superblock_state, sizeof(struct EXT2Superblock));
        write_blocks(&meta_buf, SUPERBLOCK_LBA, 1);
        memcpy(meta_buf.buf, &_ext2_bgdt_state.table[0], sizeof(struct EXT2BlockGroupDescriptor));
        write_blocks(&meta_buf, BGDT_LBA, 1);
    }
}

static int8_t add_entry_to_dir(uint32_t parent_inode_num, uint32_t new_inode_num, char *name, uint8_t name_len, uint8_t file_type) {
    struct EXT2Inode parent;
    if (!read_inode(parent_inode_num, &parent)) return -1;
    
    uint16_t need_len = get_entry_record_len(name_len);
    struct BlockBuffer buf;

    for (int i = 0; i < 12; i++) {
        if (parent.i_block[i] == 0) {
            uint32_t new_blk = allocate_block(0);
            if (new_blk == 0) return -1; 
            
            parent.i_block[i] = new_blk;
            parent.i_blocks += (BLOCK_SIZE/512);
            parent.i_size += BLOCK_SIZE;
            write_inode(parent_inode_num, &parent);
            
            memset(buf.buf, 0, BLOCK_SIZE);
            struct EXT2DirectoryEntry *ent = (struct EXT2DirectoryEntry *)buf.buf;
            ent->inode = new_inode_num;
            ent->name_len = name_len;
            ent->file_type = file_type;
            ent->rec_len = BLOCK_SIZE; 
            memcpy(get_entry_name(ent), name, name_len);
            
            write_blocks(&buf, new_blk, 1);
            return 0;
        }

        read_blocks(&buf, parent.i_block[i], 1);
        uint32_t offset = 0;
        
        while (offset < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *ent = get_directory_entry(buf.buf, offset);
            uint16_t actual_len = get_entry_record_len(ent->name_len);
            
            if (ent->rec_len >= actual_len + need_len) {
                uint16_t remaining = ent->rec_len - actual_len;
                ent->rec_len = actual_len; 
                
                struct EXT2DirectoryEntry *new_ent = (struct EXT2DirectoryEntry *)((uint8_t*)ent + actual_len);
                new_ent->inode = new_inode_num;
                new_ent->name_len = name_len;
                new_ent->file_type = file_type;
                new_ent->rec_len = remaining; 
                memcpy(get_entry_name(new_ent), name, name_len);
                
                write_blocks(&buf, parent.i_block[i], 1);
                return 0; 
            }
            
            offset += ent->rec_len;
            if (ent->rec_len == 0) break; 
        }
    }
    return -1; 
}

static int8_t remove_entry_from_dir(uint32_t parent_inode_num, char *name_to_remove, uint8_t name_len_to_remove) {
    struct EXT2Inode parent_inode;
    if (!read_inode(parent_inode_num, &parent_inode)) return -1;
    
    struct BlockBuffer buf;
    
    for (int i = 0; i < 12 && parent_inode.i_block[i] != 0; i++) {
        read_blocks(&buf, parent_inode.i_block[i], 1);
        
        uint32_t offset = 0;
        struct EXT2DirectoryEntry *prev_ent = NULL;
        struct EXT2DirectoryEntry *ent = NULL;
        
        while (offset < BLOCK_SIZE) {
            ent = get_directory_entry(buf.buf, offset);
            if (ent->rec_len == 0) break; 

            if (ent->inode != 0 && ent->name_len == name_len_to_remove) {
                char *ename = get_entry_name(ent);
                if (memcmp(ename, name_to_remove, name_len_to_remove) == 0) {
                    if (prev_ent != NULL) {
                        prev_ent->rec_len += ent->rec_len;
                    } else {
                        ent->inode = 0; 
                    }
                    write_blocks(&buf, parent_inode.i_block[i], 1);
                    return 0; 
                }
            }
            prev_ent = ent;
            offset += ent->rec_len;
        }
    }
    return -1; 
}

// ==============================================================================
// FILESYSTEM INITIALIZER
// ==============================================================================

bool is_empty_storage(void) {
    struct BlockBuffer boot_sector_buffer;
    read_blocks(&boot_sector_buffer, BOOT_SECTOR, 1);
    return memcmp(boot_sector_buffer.buf, fs_signature, BLOCK_SIZE) != 0;
}

void create_ext2(void) {
    struct BlockBuffer buffer;

    // 1. Write Signature
    memset(buffer.buf, 0, BLOCK_SIZE);
    memcpy(buffer.buf, fs_signature, sizeof(fs_signature));
    write_blocks(&buffer, BOOT_SECTOR, 1);

    // 2. Setup & Write Superblock
    memset(&_ext2_superblock_state, 0, sizeof(struct EXT2Superblock));
    _ext2_superblock_state.s_inodes_count      = TOTAL_INODES;
    _ext2_superblock_state.s_blocks_count      = TOTAL_BLOCKS;
    _ext2_superblock_state.s_free_blocks_count = TOTAL_BLOCKS - INITIAL_USED_BLOCKS;
    _ext2_superblock_state.s_free_inodes_count = TOTAL_INODES - INITIAL_USED_INODES;
    _ext2_superblock_state.s_first_data_block  = BGDT_LBA + 1; 
    _ext2_superblock_state.s_log_block_size    = 0; 
    _ext2_superblock_state.s_inodes_per_group  = INODES_PER_GROUP;
    _ext2_superblock_state.s_blocks_per_group  = BLOCKS_PER_GROUP;
    _ext2_superblock_state.s_magic             = EXT2_SUPER_MAGIC;
    _ext2_superblock_state.s_first_ino         = EXT2_GOOD_OLD_FIRST_INO;
    _ext2_superblock_state.s_inode_size        = sizeof(struct EXT2Inode);

    memset(buffer.buf, 0, BLOCK_SIZE);
    memcpy(buffer.buf, &_ext2_superblock_state, sizeof(struct EXT2Superblock));
    write_blocks(&buffer, SUPERBLOCK_LBA, 1);

    // 3. Setup & Write BGDT
    memset(&_ext2_bgdt_state, 0, sizeof(struct EXT2BlockGroupDescriptorTable));
    _ext2_bgdt_state.table[0].bg_block_bitmap      = BLOCK_BITMAP_LBA;
    _ext2_bgdt_state.table[0].bg_inode_bitmap      = INODE_BITMAP_LBA;
    _ext2_bgdt_state.table[0].bg_inode_table       = INODE_TABLE_LBA;
    _ext2_bgdt_state.table[0].bg_free_blocks_count = BLOCKS_PER_GROUP - INITIAL_USED_BLOCKS;
    _ext2_bgdt_state.table[0].bg_free_inodes_count = INODES_PER_GROUP - INITIAL_USED_INODES;
    _ext2_bgdt_state.table[0].bg_used_dirs_count   = 1; 

    memset(buffer.buf, 0, BLOCK_SIZE);
    memcpy(buffer.buf, &_ext2_bgdt_state.table[0], sizeof(struct EXT2BlockGroupDescriptor));
    write_blocks(&buffer, BGDT_LBA, 1);

    // 4. Bitmaps
    memset(buffer.buf, 0, BLOCK_SIZE);
    for (uint32_t i = 0; i < INITIAL_USED_BLOCKS; ++i) set_bit(buffer.buf, i);
    write_blocks(&buffer, BLOCK_BITMAP_LBA, 1);

    memset(buffer.buf, 0, BLOCK_SIZE);
    for (uint32_t i = 1; i <= INITIAL_USED_INODES; ++i) set_bit(buffer.buf, i - 1);
    write_blocks(&buffer, INODE_BITMAP_LBA, 1);

    // 5. Inode Table (Root)
    memset(buffer.buf, 0, BLOCK_SIZE);
    for (uint32_t i = 0; i < INODES_TABLE_BLOCK_COUNT; i++) {
        write_blocks(&buffer, INODE_TABLE_LBA + i, 1);
    }
    
    struct EXT2Inode root_inode;
    memset(&root_inode, 0, sizeof(struct EXT2Inode));
    root_inode.i_mode  = EXT2_S_IFDIR | 0755;
    root_inode.i_size  = BLOCK_SIZE;
    root_inode.i_links_count = 2;
    root_inode.i_blocks = BLOCK_SIZE / 512;
    root_inode.i_block[0] = ROOT_DIR_BLOCK_LBA;
    write_inode(ROOT_INODE_NO, &root_inode);

    // 6. Root Data Block
    memset(buffer.buf, 0, BLOCK_SIZE);
    struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)buffer.buf;

    entry->inode = ROOT_INODE_NO;
    entry->rec_len = get_entry_record_len(1);
    entry->name_len = 1;
    entry->file_type = EXT2_FT_DIR;
    memcpy(get_entry_name(entry), ".", 1);
    uint16_t len_dot = entry->rec_len;

    entry = get_next_directory_entry(entry);
    entry->inode = ROOT_INODE_NO;
    entry->name_len = 2;
    entry->file_type = EXT2_FT_DIR;
    memcpy(get_entry_name(entry), "..", 2);
    entry->rec_len = BLOCK_SIZE - len_dot;

    write_blocks(&buffer, ROOT_DIR_BLOCK_LBA, 1);

    g_filesystem_initialized = true;
}

void initialize_filesystem_ext2(void) {
    if (is_empty_storage()) {
        create_ext2();
        g_filesystem_initialized = true;
    } else {
        read_blocks(&_ext2_superblock_state, SUPERBLOCK_LBA, 1);
        read_blocks(&_ext2_bgdt_state.table[0], BGDT_LBA, 1);
        if (_ext2_superblock_state.s_magic == EXT2_SUPER_MAGIC) {
            g_filesystem_initialized = true;
        }
    }
}

// ==============================================================================
// MEMORY MANAGEMENT (ALLOCATION)
// ==============================================================================

uint32_t allocate_node(void) {
    if (!g_filesystem_initialized) return 0;

    struct BlockBuffer bitmap;
    uint32_t bgd_idx = 0;
    if (_ext2_bgdt_state.table[bgd_idx].bg_free_inodes_count == 0) return 0;

    read_blocks(&bitmap, INODE_BITMAP_LBA, 1);

    for (uint32_t i = 0; i < INODES_PER_GROUP; i++) {
        if (test_bit(bitmap.buf, i) == 0) {
            set_bit(bitmap.buf, i);
            write_blocks(&bitmap, INODE_BITMAP_LBA, 1);

            _ext2_superblock_state.s_free_inodes_count--;
            _ext2_bgdt_state.table[bgd_idx].bg_free_inodes_count--;
            
            struct BlockBuffer meta_buf;
            memcpy(meta_buf.buf, &_ext2_superblock_state, sizeof(struct EXT2Superblock));
            write_blocks(&meta_buf, SUPERBLOCK_LBA, 1);
            memcpy(meta_buf.buf, &_ext2_bgdt_state.table[0], sizeof(struct EXT2BlockGroupDescriptor));
            write_blocks(&meta_buf, BGDT_LBA, 1);

            return i + 1;
        }
    }
    return 0;
}

void deallocate_node(uint32_t inode) {
    if (!g_filesystem_initialized || inode == 0) return;
    
    struct EXT2Inode node_data;
    if (!read_inode(inode, &node_data)) return;
    
    for (int i = 0; i < 12; i++) {
        if (node_data.i_block[i] != 0) {
            deallocate_single_block(node_data.i_block[i]);
            node_data.i_block[i] = 0;
        }
    }
    
    memset(&node_data, 0, sizeof(struct EXT2Inode));
    write_inode(inode, &node_data);

    struct BlockBuffer bitmap;
    read_blocks(&bitmap, INODE_BITMAP_LBA, 1);
    clear_bit(bitmap.buf, inode - 1);
    write_blocks(&bitmap, INODE_BITMAP_LBA, 1);

    _ext2_superblock_state.s_free_inodes_count++;
    _ext2_bgdt_state.table[0].bg_free_inodes_count++;
    
    struct BlockBuffer meta_buf;
    memcpy(meta_buf.buf, &_ext2_superblock_state, sizeof(struct EXT2Superblock));
    write_blocks(&meta_buf, SUPERBLOCK_LBA, 1);
    memcpy(meta_buf.buf, &_ext2_bgdt_state.table[0], sizeof(struct EXT2BlockGroupDescriptor));
    write_blocks(&meta_buf, BGDT_LBA, 1);
}

void allocate_node_blocks(void *ptr, struct EXT2Inode *node, uint32_t prefered_bgd) {
    (void)ptr; (void)node; (void)prefered_bgd;
}

void deallocate_blocks(void *loc, uint32_t blocks) {
    (void)loc; (void)blocks;
}

uint32_t deallocate_block(uint32_t *locations, uint32_t blocks, struct BlockBuffer *bitmap, uint32_t depth, uint32_t *last_bgd, bool bgd_loaded) {
    (void)locations; (void)blocks; (void)bitmap; (void)depth; (void)last_bgd; (void)bgd_loaded;
    return 0;
}

void sync_node(struct EXT2Inode *node, uint32_t inode) {
    write_inode(inode, node);
}

// ==============================================================================
// CRUD OPERATIONS
// ==============================================================================

int8_t read_directory(struct EXT2DriverRequest *prequest) {
    if (!g_filesystem_initialized) return -1;
    
    struct EXT2Inode dir_inode;
    if (!read_inode(prequest->parent_inode, &dir_inode)) return 3;
    if ((dir_inode.i_mode & EXT2_S_IFDIR) == 0) return 1;

    if (dir_inode.i_block[0] == 0) return 0;

    struct BlockBuffer buf;
    read_blocks(&buf, dir_inode.i_block[0], 1);
    
    uint32_t copy_size = (BLOCK_SIZE < prequest->buffer_size) ? BLOCK_SIZE : prequest->buffer_size;
    memcpy(prequest->buf, buf.buf, copy_size);
    
    return 0;
}

int8_t read(struct EXT2DriverRequest request) {
    if (!g_filesystem_initialized) return -1;
    if (request.parent_inode == 0) return 4;

    uint32_t file_inode_num = find_entry_in_dir(request.parent_inode, request.name, request.name_len);
    if (file_inode_num == 0) return 3; 

    struct EXT2Inode file_inode;
    if (!read_inode(file_inode_num, &file_inode)) return -1;

    if (file_inode.i_mode & EXT2_S_IFDIR) return 1; 

    uint32_t bytes_read = 0;
    uint32_t bytes_to_read = (request.buffer_size < file_inode.i_size) ? request.buffer_size : file_inode.i_size;
    uint8_t *out = (uint8_t *)request.buf;
    struct BlockBuffer buf;

    for (int i = 0; i < 12 && bytes_read < bytes_to_read; i++) {
        if (file_inode.i_block[i] == 0) break;
        
        read_blocks(&buf, file_inode.i_block[i], 1);
        
        uint32_t chunk = BLOCK_SIZE;
        if (bytes_to_read - bytes_read < BLOCK_SIZE) {
            chunk = bytes_to_read - bytes_read;
        }
        
        memcpy(out + bytes_read, buf.buf, chunk);
        bytes_read += chunk;
    }
    
    return 0;
}

int8_t write(struct EXT2DriverRequest *request) {
    if (!g_filesystem_initialized) return -1;
    if (request->parent_inode == 0) return 2;

    if (find_entry_in_dir(request->parent_inode, request->name, request->name_len) != 0) {
        return 1; 
    }

    uint32_t new_inode_num = allocate_node();
    if (new_inode_num == 0) return -1;

    struct EXT2Inode new_inode;
    memset(&new_inode, 0, sizeof(struct EXT2Inode));
    if (request->is_directory) {
        new_inode.i_mode = EXT2_S_IFDIR | 0755;
        new_inode.i_links_count = 2;
    } else {
        new_inode.i_mode = EXT2_S_IFREG | 0644;
        new_inode.i_links_count = 1;
        new_inode.i_size = request->buffer_size;
    }

    if (!request->is_directory && request->buffer_size > 0) {
        uint32_t blocks_needed = (request->buffer_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
        uint8_t *src = (uint8_t *)request->buf;
        
        for (uint32_t i = 0; i < blocks_needed && i < 12; i++) {
            uint32_t blk = allocate_block(0);
            if (blk == 0) break; 
            
            new_inode.i_block[i] = blk;
            new_inode.i_blocks += (BLOCK_SIZE/512);
            
            struct BlockBuffer data;
            memset(data.buf, 0, BLOCK_SIZE);
            uint32_t copy_sz = (i == blocks_needed - 1) ? (request->buffer_size % BLOCK_SIZE) : BLOCK_SIZE;
            if (copy_sz == 0) copy_sz = BLOCK_SIZE;
            
            memcpy(data.buf, src + (i * BLOCK_SIZE), copy_sz);
            write_blocks(&data, blk, 1);
        }
    }
    else if (request->is_directory) {
        uint32_t blk = allocate_block(0);
        if (blk != 0) {
            new_inode.i_block[0] = blk;
            new_inode.i_blocks = (BLOCK_SIZE/512);
            new_inode.i_size = BLOCK_SIZE;
            
            struct BlockBuffer dirdata;
            memset(dirdata.buf, 0, BLOCK_SIZE);
            
            struct EXT2DirectoryEntry *dot = (struct EXT2DirectoryEntry *)dirdata.buf;
            dot->inode = new_inode_num;
            dot->name_len = 1;
            dot->file_type = EXT2_FT_DIR;
            dot->rec_len = get_entry_record_len(1);
            memcpy(get_entry_name(dot), ".", 1);
            
            struct EXT2DirectoryEntry *dotdot = get_next_directory_entry(dot);
            dotdot->inode = request->parent_inode;
            dotdot->name_len = 2;
            dotdot->file_type = EXT2_FT_DIR;
            dotdot->rec_len = BLOCK_SIZE - dot->rec_len;
            memcpy(get_entry_name(dotdot), "..", 2);
            
            write_blocks(&dirdata, blk, 1);
        }
    }

    write_inode(new_inode_num, &new_inode);

    uint8_t type = request->is_directory ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
    if (add_entry_to_dir(request->parent_inode, new_inode_num, request->name, request->name_len, type) != 0) {
        return -1; 
    }

    return 0;
}

bool is_directory_empty(uint32_t inode) {
    struct EXT2Inode dir_inode;
    if (!read_inode(inode, &dir_inode)) return true;
    if ((dir_inode.i_mode & EXT2_S_IFDIR) == 0) return true;

    struct BlockBuffer buf;
    for (int i = 0; i < 12; i++) {
        if (dir_inode.i_block[i] == 0) break;
        
        read_blocks(&buf, dir_inode.i_block[i], 1);
        
        uint32_t offset = 0;
        if (i == 0) offset = get_dir_first_child_offset(buf.buf);
        
        while (offset < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *ent = get_directory_entry(buf.buf, offset);
            if (ent->rec_len == 0) break; 
            
            if (ent->inode != 0) {
                return false; 
            }
            offset += ent->rec_len;
        }
    }
    return true; 
}

int8_t delete(struct EXT2DriverRequest request) {
    if (!g_filesystem_initialized) return -1;
    if (request.parent_inode == 0) return 3; 
    if (request.name == NULL || request.name_len == 0) return -1;

    uint32_t target_inode_num = find_entry_in_dir(request.parent_inode, request.name, request.name_len);
    if (target_inode_num == 0) return 1; 

    struct EXT2Inode target_inode;
    if (!read_inode(target_inode_num, &target_inode)) return -1;

    bool is_dir = (target_inode.i_mode & EXT2_S_IFDIR);
    
    if (request.is_directory) {
        if (!is_dir) return 1; 
        if (!is_directory_empty(target_inode_num)) return 2; 
    } else {
        if (is_dir) return 1; 
    }

    if (remove_entry_from_dir(request.parent_inode, request.name, request.name_len) != 0) {
        return -1; 
    }

    deallocate_node(target_inode_num);

    return 0; 
}