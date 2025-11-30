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
 * Page Directory Entry, for page size 4 MB.
 * Check Intel Manual 3a - Ch 4 Paging - Figure 4-4 PDE: 4MB page
 *
 * Total: 32-bit (4 Byte)
 * @param present_bit       1: Halaman ada di memori, 0: Tidak ada (Page Fault)
 * @param write_bit         1: Read/Write, 0: Read-only
 * @param user_bit          1: User Mode (Ring 3), 0: Kernel Mode (Ring 0)
 * @param pwt_bit           Page-level Write-Through (Caching policy)
 * @param pcd_bit           Page-level Cache Disable (Caching policy)
 * @param accessed_bit      1: Halaman pernah diakses (dibaca/tulis) oleh CPU
 * @param dirty_bit         1: Halaman pernah ditulis (modifikasi) oleh CPU
 * @param use_pagesize_4_mb 1: Menggunakan ukuran halaman 4 MB (WAJIB 1 di tubes ini)
 * @param global_page       Global Page (Ignored if CR4.PGE = 0)
 * @param available         Available for OS (Ignored by hardware)
 * @param pat               PAT (Page Attribute Table)
 * @param reserved_high     Reserved (Must be 0 for 32-bit non-PAE)
 * @param lower_address     Address (10-bit High Address) -> Menunjuk ke fisik 4MB-aligned
 */
struct PageDirectoryEntry {
    uint32_t present_bit        : 1;
    uint32_t write_bit          : 1;
    uint32_t user_bit           : 1;
    uint32_t pwt_bit            : 1;
    uint32_t pcd_bit            : 1;
    uint32_t accessed_bit       : 1;
    uint32_t dirty_bit          : 1;
    uint32_t use_pagesize_4_mb  : 1;
    uint32_t global_page        : 1;
    uint32_t available          : 3;
    uint32_t pat                : 1;
    uint32_t reserved_high      : 9;
    uint32_t lower_address      : 10;
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