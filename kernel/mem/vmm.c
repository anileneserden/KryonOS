#include <kernel/mem/vmm.h>
#include <kernel/mem/pmm.h>
#include <kernel/serial.h>

static uint32_t* kernel_page_directory = 0;

extern void load_page_directory(uint32_t* pd);
extern void enable_paging(void);

void vmm_init(void) {
    serial_write("VMM: Virtual Memory Manager (4MB large pages) starting...\n");

    // 1. Step: enable the PSE (Page Size Extension - 4MB page support) bit in CR4 (bit 4)
    __asm__ volatile(
        "mov %%cr4, %%eax\n\t"
        "or $0x10, %%eax\n\t"
        "mov %%eax, %%cr4\n\t"
        ::: "eax"
    );

    // Allocate one block (4KB) from the PMM for the page directory
    kernel_page_directory = (uint32_t*)pmm_alloc_block();
    
    // 2. Step: identity-map the entire 4GB address space with 4MB large pages
    for (int i = 0; i < 1024; i++) {
        uint32_t physical_addr = (uint32_t)i * 0x400000; // Each entry represents 4MB
        
        // Base flags: Present (1) | Write (2) | Page Size - 4MB (0x80)
        uint32_t flags = PAGE_PRESENT | PAGE_WRITE | 0x80;
        
        // Since the PMM assumes 32MB of RAM, all regions beyond 32MB (MMIO, framebuffer, etc.)
        // must be uncacheable. PCD (Page-level Cache Disable) bit = 0x10
        if (physical_addr >= 32 * 1024 * 1024) {
            flags |= 0x10; 
        }

        kernel_page_directory[i] = physical_addr | flags;
    }

    // Load the page directory into CR3 and enable paging
    vmm_switch_directory(kernel_page_directory);
    enable_paging();

    serial_write("VMM: Entire 4GB address space mapped successfully and paging enabled.\n");
}

void vmm_switch_directory(uint32_t* pd) {
    kernel_page_directory = pd;
    __asm__ volatile("mov %0, %%cr3" : : "r"(pd));
}

void vmm_map_page(uint32_t physical_addr, uint32_t virtual_addr, uint32_t flags) {
    uint32_t pd_index = virtual_addr >> 22;
    uint32_t pt_index = (virtual_addr >> 12) & 0x03FF;

    uint32_t* page_directory = kernel_page_directory;

    if (!(page_directory[pd_index] & PAGE_PRESENT)) {
        uint32_t* new_table = (uint32_t*)pmm_alloc_block();
        for (int i = 0; i < 1024; i++) {
            new_table[i] = 0x00000002;
        }
        page_directory[pd_index] = ((uint32_t)new_table) | PAGE_PRESENT | PAGE_WRITE | (flags & PAGE_USER);
    }

    uint32_t* page_table = (uint32_t*)(page_directory[pd_index] & 0xFFFFF000);
    page_table[pt_index] = (physical_addr & 0xFFFFF000) | (flags & 0xFFF) | PAGE_PRESENT;
}