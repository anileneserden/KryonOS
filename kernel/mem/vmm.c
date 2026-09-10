#include <kernel/mem/vmm.h>
#include <kernel/mem/pmm.h>
#include <kernel/serial.h>

static uint32_t* kernel_page_directory = 0;

extern void load_page_directory(uint32_t* pd);
extern void enable_paging(void);

void vmm_init(void) {
    serial_write("VMM: Sanal Bellek Yoneticisi (4MB Buyuk Sayfalar) baslatiliyor...\n");

    // 1. Adim: CR4 register'inda PSE (Page Size Extension - 4MB sayfa destegi) bitini aktif et (Bit 4)
    __asm__ volatile(
        "mov %%cr4, %%eax\n\t"
        "or $0x10, %%eax\n\t"
        "mov %%eax, %%cr4\n\t"
        ::: "eax"
    );

    // PMM'den Page Directory için 1 blok (4KB) ayır
    kernel_page_directory = (uint32_t*)pmm_alloc_block();
    
    // 2. Adim: Tum 4GB adres uzayini 4MB'lik buyuk sayfalarla identity-map yap 
    // (1024 giris * 4MB = 4GB. Framebuffer, MMIO ve tum RAM kapsanir)
    for (int i = 0; i < 1024; i++) {
        uint32_t physical_addr = i * 0x400000; // Her giris 4MB temsil eder
        
        // Adres | Present (1) | Write (2) | Page Size - 4MB (0x80)
        kernel_page_directory[i] = physical_addr | PAGE_PRESENT | PAGE_WRITE | 0x80;
    }

    // Sayfa dizinini CR3'e yükle ve paging'i aktif et
    vmm_switch_directory(kernel_page_directory);
    enable_paging();

    serial_write("VMM: Tum 4GB adres uzayi basariyla haritalandirildi ve paging aktiflestirildi.\n");
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