#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../header/memory/paging.h" 

__attribute__((aligned(0x1000))) struct PageDirectory _paging_kernel_page_directory = {
    .table = {
        // Identity Map 0-4MB (Virtual 0x0 -> Physical 0x0)
        [0] = {
            .flag.present_bit       = 1,
            .flag.write_bit         = 1,
            .flag.use_pagesize_4_mb = 1,
            .lower_address          = 0,
        },
        // Higher Half Kernel Map (Virtual 0xC0000000 -> Physical 0x0)
        // 0x300 desimal = 768. 768 * 4MB = 3GB (0xC0000000)
        [0x300] = {
            .flag.present_bit       = 1,
            .flag.write_bit         = 1,
            .flag.use_pagesize_4_mb = 1,
            .lower_address          = 0,
        },
    }
};

static struct PageManagerState page_manager_state = {
    .page_frame_map = {
        [0]                            = true, // Frame 0 (0-4MB) sudah dipakai Kernel
        [1 ... PAGE_FRAME_MAX_COUNT-1] = false // Sisanya bebas
    },
    // Total frame dikurangi 1 (karena frame 0 dipake)
    .free_page_frame_count = PAGE_FRAME_MAX_COUNT - 1
};

void update_page_directory_entry(
    struct PageDirectory *page_dir,
    void *physical_addr, 
    void *virtual_addr, 
    struct PageDirectoryEntryFlag flag
) {
    // Ambil index page directory (10 bit teratas dari Virtual Address)
    uint32_t page_index = ((uint32_t) virtual_addr >> 22) & 0x3FF;
    
    page_dir->table[page_index].flag = flag;
    
    // Ambil 10 bit teratas dari Physical Address untuk lower_address
    // Ingat: lower_address di struct cuma 10 bit
    page_dir->table[page_index].lower_address = ((uint32_t) physical_addr >> 22) & 0x3FF;
    
    flush_single_tlb(virtual_addr);
}

void flush_single_tlb(void *virtual_addr) {
    asm volatile("invlpg (%0)" : /* <Empty> */ : "b"(virtual_addr): "memory");
}

/* --- Memory Management --- */

bool paging_allocate_check(uint32_t amount) {
    // Hitung berapa frame (4MB) yang dibutuhkan
    // Rumus ceil: (amount + PAGE_FRAME_SIZE - 1) / PAGE_FRAME_SIZE
    uint32_t required_frames = (amount + PAGE_FRAME_SIZE - 1) / PAGE_FRAME_SIZE;
    
    // Cek apakah frame yang tersedia cukup
    return required_frames <= page_manager_state.free_page_frame_count;
}

bool paging_allocate_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr) {
    // 1. Cari frame fisik yang kosong di bitmap manager
    // Mulai dari 1 karena frame 0 (0-4MB) reserved buat Kernel
    uint32_t physical_frame_idx = 0;
    bool found = false;

    for (uint32_t i = 1; i < PAGE_FRAME_MAX_COUNT; i++) {
        if (!page_manager_state.page_frame_map[i]) {
            physical_frame_idx = i;
            found = true;
            break;
        }
    }

    if (!found) {
        return false; // Memori penuh (Out of Memory)
    }

    // 2. Tandai frame sebagai terpakai
    page_manager_state.page_frame_map[physical_frame_idx] = true;
    page_manager_state.free_page_frame_count--;

    // 3. Siapkan Flags untuk User Mode
    struct PageDirectoryEntryFlag flags = {
        .present_bit       = 1,
        .write_bit         = 1,
        .user_bit          = 1, // PENTING: Izinkan akses dari Ring 3
        .use_pagesize_4_mb = 1  // PENTING: Kita pake 4MB pages
    };

    // 4. Hitung alamat fisik: Index * 4 MB
    void *physical_addr = (void*)(physical_frame_idx * PAGE_FRAME_SIZE);

    // 5. Update Page Directory dengan mapping baru
    update_page_directory_entry(page_dir, physical_addr, virtual_addr, flags);

    return true;
}

bool paging_free_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr) {
    // Dapatkan index dari virtual address
    uint32_t page_index = ((uint32_t) virtual_addr >> 22) & 0x3FF;

    // Cek apakah entry tersebut valid/present
    if (!page_dir->table[page_index].flag.present_bit) {
        return false; // Tidak ada yang perlu di-free
    }

    // Dapatkan index frame fisik dari entry tersebut
    uint32_t physical_frame_idx = page_dir->table[page_index].lower_address;

    // Tandai frame fisik sebagai bebas di manager
    if (page_manager_state.page_frame_map[physical_frame_idx]) {
        page_manager_state.page_frame_map[physical_frame_idx] = false;
        page_manager_state.free_page_frame_count++;
    }

    // Kosongkan entry di Page Directory (set ke 0)
    struct PageDirectoryEntryFlag empty_flag = {0};
    page_dir->table[page_index].flag = empty_flag;
    page_dir->table[page_index].lower_address = 0;

    // Flush TLB agar CPU sadar mappingnya udah ilang
    flush_single_tlb(virtual_addr);

    return true;
}