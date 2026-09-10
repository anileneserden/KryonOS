#include <kernel/mem/pmm.h>
#include <kernel/serial.h>

// Bitmap'i çekirdekten ve ilk alanlardan çok güvenli bir uzakta (4MB) başlatıyoruz
static uint32_t* memory_map = (uint32_t*)0x400000; 
static uint32_t total_blocks = 0;
static uint32_t used_blocks = 0;

static inline void mmap_set(int bit) {
    memory_map[bit / 32] |= (1 << (bit % 32));
}

static inline void mmap_unset(int bit) {
    memory_map[bit / 32] &= ~(1 << (bit % 32));
}

void pmm_init(multiboot_info_t* mboot) {
    (void)mboot;
    serial_write("PMM: Fiziksel Bellek Yoneticisi baslatiliyor...\n");

    // 32MB RAM kabul edelim
    total_blocks = (32 * 1024 * 1024) / PAGE_SIZE;
    used_blocks = total_blocks;

    // Bitmap'i tamamen dolu (rezerve) olarak başlat
    for (uint32_t i = 0; i < (total_blocks / 32); i++) {
        memory_map[i] = 0xFFFFFFFF;
    }

    // 5MB (0x500000) adresinden sonrasını serbest bırakıyoruz
    // İlk 5MB; çekirdek, bootloader, framebuffer ve PMM bitmap alanı için güvenle rezerve kalır.
    uint32_t start_free_block = 0x500000 / PAGE_SIZE; 
    uint32_t end_free_block = total_blocks;

    for (uint32_t b = start_free_block; b < end_free_block; b++) {
        mmap_unset(b);
        used_blocks--;
    }

    serial_write("PMM: Bellek haritasi basariyla olusturuldu (Guvenli Mod v2).\n");
}

void* pmm_alloc_block(void) {
    if ((total_blocks - used_blocks) <= 0) return 0;

    for (uint32_t i = 0; i < total_blocks / 32; i++) {
        if (memory_map[i] != 0xFFFFFFFF) {
            for (int j = 0; j < 32; j++) {
                int bit = 1 << j;
                if (!(memory_map[i] & bit)) {
                    int frame = i * 32 + j;
                    mmap_set(frame);
                    used_blocks++;
                    return (void*)(frame * PAGE_SIZE);
                }
            }
        }
    }
    return 0;
}

void pmm_free_block(void* b) {
    uint32_t frame = (uint32_t)b / PAGE_SIZE;
    mmap_unset(frame);
    used_blocks--;
}