#ifndef _PAGING_H
#define _PAGING_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Note: MB often referring to MiB in context of memory management
#define SYSTEM_MEMORY_MB     128

#define PAGE_ENTRY_COUNT     1024
// Page Frame (PF) Size: (1 << 22) B = 4*1024*1024 B = 4 MiB
#define PAGE_FRAME_SIZE      (1 << (2 + 10 + 10))
// Maximum usable page frame. Default count: 128 / 4 = 32 page frame
#define PAGE_FRAME_MAX_COUNT ((SYSTEM_MEMORY_MB << 20) / PAGE_FRAME_SIZE)

// Operating system page directory, using page size PAGE_FRAME_SIZE (4 MiB)
extern struct PageDirectory _paging_kernel_page_directory;


/**
 * Page Directory Entry Flag, only first 8 bit
 * Referensi: Intel Manual 3a - 4.3 32-BIT PAGING - Figure 4-4
 * * @param present_bit       1: Halaman ada di memori, 0: Tidak ada (Page Fault)
 * @param write_bit         1: Read/Write, 0: Read-only
 * @param user_bit          1: User Mode (Ring 3), 0: Kernel Mode (Ring 0)
 * @param pwt_bit           Page-level Write-Through (Caching policy)
 * @param pcd_bit           Page-level Cache Disable (Caching policy)
 * @param accessed_bit      1: Halaman pernah diakses (dibaca/tulis) oleh CPU
 * @param dirty_bit         1: Halaman pernah ditulis (modifikasi) oleh CPU
 * @param use_pagesize_4_mb 1: Menggunakan ukuran halaman 4 MB (WAJIB 1 di tubes ini)
 */
struct PageDirectoryEntryFlag {
    uint8_t present_bit        : 1;
    uint8_t write_bit          : 1;
    uint8_t user_bit           : 1;
    uint8_t pwt_bit            : 1;
    uint8_t pcd_bit            : 1;
    uint8_t accessed_bit       : 1;
    uint8_t dirty_bit          : 1;
    uint8_t use_pagesize_4_mb  : 1; // Bit ke-7 (PS Bit)
} __attribute__((packed));

/**
 * Page Directory Entry, for page size 4 MB.
 * Check Intel Manual 3a - Ch 4 Paging - Figure 4-4 PDE: 4MB page
 *
 * Total: 32-bit (4 Byte)
 * - Bit 0-7   : Flags (struct PageDirectoryEntryFlag)
 * - Bit 8     : Global Page (Ignored if CR4.PGE = 0)
 * - Bit 9-11  : Available for OS (Ignored by hardware)
 * - Bit 12    : PAT (Page Attribute Table)
 * - Bit 13-21 : Reserved (Must be 0 for 32-bit non-PAE)
 * - Bit 22-31 : Address (10-bit High Address) -> Menunjuk ke fisik 4MB-aligned
 */
struct PageDirectoryEntry {
    struct PageDirectoryEntryFlag flag;
    uint16_t global_page    : 1;
    uint16_t available      : 3;
    uint16_t pat            : 1;
    uint16_t reserved_high  : 9;
    uint16_t lower_address  : 10;
} __attribute__((packed));

/**
 * Page Directory, contain array of PageDirectoryEntry.
 * Warning: Address must be aligned in 4 KB (listed on Intel Manual), use __attribute__((aligned(0x1000))), 
 * unaligned definition of PageDirectory will cause triple fault
 * * @param table Fixed-width array of PageDirectoryEntry with size PAGE_ENTRY_COUNT
 */
struct PageDirectory {
    struct PageDirectoryEntry table[PAGE_ENTRY_COUNT];
} __attribute__((packed, aligned(0x1000)));

/**
 * Containing page manager states.
 * * @param page_frame_map Keeping track empty space. True when the page frame is currently used
 */
struct PageManagerState {
    bool     page_frame_map[PAGE_FRAME_MAX_COUNT];
    uint32_t free_page_frame_count;
} __attribute__((packed));


/**
 * Edit page directory with respective parameter
 * * @param page_dir      Page directory to update
 * @param physical_addr Physical address to map
 * @param virtual_addr  Virtual address to map
 * @param flag          Page entry flags
 */
void update_page_directory_entry(
    struct PageDirectory *page_dir,
    void *physical_addr, 
    void *virtual_addr, 
    struct PageDirectoryEntryFlag flag
);

/**
 * Invalidate page that contain virtual address in parameter
 * * @param virtual_addr Virtual address to flush
 */
void flush_single_tlb(void *virtual_addr);

/* --- Memory Management --- */
/**
 * Check whether a certain amount of physical memory is available
 * * @param amount Requested amount of physical memory in bytes
 * @return       Return true when there's enough free memory available
 */
bool paging_allocate_check(uint32_t amount);

/**
 * Allocate single user page frame in page directory
 * * @param page_dir     Page directory to update
 * @param virtual_addr Virtual address to be allocated
 * @return             Physical address of allocated frame
 */
bool paging_allocate_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr);

/**
 * Deallocate single user page frame in page directory
 * * @param page_dir      Page directory to update
 * @param virtual_addr  Virtual address to be allocated
 * @return              Will return true if success, false otherwise
 */
bool paging_free_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr);

#endif