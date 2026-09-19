#include <kernel/fs/kryfs.h>
#include <kernel/fs/vfs.h>
#include <kernel/drivers/storage/ata.h>
#include <kernel/serial.h>
#include <kernel/mem/heap.h>
#include <kernel/string.h>

static kryfs_superblock_t sb_cache;
static int kryfs_is_mounted = 0;

int kryfs_mount(void) {
    serial_write("[KRYFS] Mount islemi baslatiliyor...\n");

    uint8_t* sector_buffer = (uint8_t*)kmalloc(512);
    if (!sector_buffer) {
        serial_write("[KRYFS HATA] Bellek tahsisi basarisiz (kmalloc)!\n");
        return -1;
    }

    // ATA sürücüsünün gerçek imzasına uyuyoruz: (drive, lba, count, buf)
    ata_read_sectors(0, 0, 1, sector_buffer);

    // Okunan veriyi cache yapısına kopyala
    memcpy(&sb_cache, sector_buffer, sizeof(kryfs_superblock_t));
    kfree(sector_buffer); // Bellek sızıntısını önlemek için serbest bırakıyoruz

    // Sihirli numara (Magic Number) kontrolü ('KRYF' -> 0x4B525946)
    if (sb_cache.magic != 0x4B525946) {
        serial_write("[KRYFS HATA] Gecersiz dosya sistemi imzasi (Magic mismatch)!\n");
        return -1;
    }

    serial_write("[KRYFS] Superblock basariyla dogrulandi!\n");
    serial_write("[KRYFS] -> Toplam Blok Sayisi: ");
    serial_write_num(sb_cache.total_blocks);
    serial_write("\n");
    serial_write("[KRYFS] -> Inode Tablo Bloku: ");
    serial_write_num(sb_cache.inode_table_block);
    serial_write("\n");

    kryfs_is_mounted = 1;
    serial_write("[KRYFS] Dosya sistemi basariyla monte edildi (Mounted)!\n");
    return 0;
}

int kryfs_find_inode(const char* filename, kryfs_inode_t* out_inode) {
    if (!kryfs_is_mounted) {
        serial_write("[KRYFS HATA] Dosya sistemi monte edilmemis!\n");
        return -1;
    }

    uint32_t table_lba = sb_cache.inode_table_block;
    uint32_t inodes_per_sector = 512 / sizeof(kryfs_inode_t);

    uint8_t* sector_buf = (uint8_t*)kmalloc(512);
    if (!sector_buf) {
        serial_write("[KRYFS HATA] Inode arama icin bellek tahsisi basarisiz!\n");
        return -1;
    }

    // Inode tablosunun ilk sektörünü oku
    ata_read_sectors(0, table_lba, 1, sector_buf);

    kryfs_inode_t* inodes = (kryfs_inode_t*)sector_buf;
    int found = -1;

    for (uint32_t i = 0; i < inodes_per_sector; i++) {
        if (inodes[i].is_used) {
            serial_write("[KRYFS] Inode taranıyor: ");
            serial_write(inodes[i].filename);
            serial_write("\n");

            if (strcmp(inodes[i].filename, filename) == 0) {
                // Dosya bulundu, dışarıdaki yapıya kopyala
                memcpy(out_inode, &inodes[i], sizeof(kryfs_inode_t));
                found = 0; // Başarılı
                break;
            }
        }
    }

    kfree(sector_buf); // Inode arama tamponunu serbest bırak

    if (found != 0) {
        serial_write("[KRYFS] Aranan dosya bulunamadi: ");
        serial_write((char*)filename);
        serial_write("\n");
    }

    return found;
}

void* kryfs_read_file(const char* filename, uint32_t* out_size) {
    if (!kryfs_is_mounted) {
        serial_write("[KRYFS HATA] Dosya sistemi monte edilmemis!\n");
        if (out_size) *out_size = 0;
        return NULL;
    }

    kryfs_inode_t inode;
    if (kryfs_find_inode(filename, &inode) != 0) {
        if (out_size) *out_size = 0;
        return NULL;
    }

    if (inode.is_directory) {
        serial_write("[KRYFS HATA] Belirtilen isim bir dosya degil, dizin!\n");
        if (out_size) *out_size = 0;
        return NULL;
    }

    // --- DETAYLI DEBUG BİLGİLERİ ---
    serial_write("[KRYFS DEBUG] Dosya Adi: ");
    serial_write((char*)filename);
    serial_write("\n[KRYFS DEBUG] Inode Size (Boyut): ");
    serial_write_num(inode.size);
    serial_write("\n[KRYFS DEBUG] Inode Start Block: ");
    serial_write_num(inode.start_block);
    serial_write("\n[KRYFS DEBUG] Data Block Start: ");
    serial_write_num(sb_cache.data_block_start);
    serial_write("\n");
    // -------------------------------

    uint32_t size = inode.size;
    uint8_t* file_buffer = (uint8_t*)kmalloc(size + 1); // Güvenli null terminator için +1
    if (!file_buffer) {
        serial_write("[KRYFS HATA] Dosya icerigi icin bellek tahsisi basarisiz!\n");
        if (out_size) *out_size = 0;
        return NULL;
    }

    // Kaç sektör okunacağını hesapla (Sektör başına 512 bayt)
    uint32_t sectors_needed = (size + 511) / 512;
    if (sectors_needed == 0) sectors_needed = 1;

    uint32_t start_lba = inode.start_block;
    
    uint8_t* sector_sec = (uint8_t*)kmalloc(512);
    if (!sector_sec) {
        kfree(file_buffer);
        if (out_size) *out_size = 0;
        return NULL;
    }

    uint32_t bytes_read = 0;
    for (uint32_t i = 0; i < sectors_needed; i++) {
        ata_read_sectors(0, start_lba + i, 1, sector_sec);
        
        uint32_t copy_len = 512;
        if (bytes_read + copy_len > size) {
            copy_len = size - bytes_read;
        }
        
        memcpy(file_buffer + bytes_read, sector_sec, copy_len);
        bytes_read += copy_len;
    }

    kfree(sector_sec);
    file_buffer[size] = '\0'; // String sonlandırıcı

    if (out_size) {
        *out_size = size;
    }

    serial_write("[KRYFS] Dosya basariyla okundu: ");
    serial_write((char*)filename);
    serial_write("\n");

    return file_buffer;
}

fs_driver_t kryfs_get_driver(void) {
    fs_driver_t driver;
    driver.read_file = kryfs_read_file;
    driver.list_dir = NULL;
    driver.get_dir_files = NULL;
    return driver;
}