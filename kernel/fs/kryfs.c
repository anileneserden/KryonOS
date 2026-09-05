#include <kernel/fs/kryfs.h>
#include <kernel/drivers/storage/ata.h>
#include <kernel/serial.h>
#include <kernel/string.h>

#define KRYFS_SUPERBLOCK_SECTOR 0

static uint8_t file_read_buffer[16384];

void kryfs_format(void) {
    serial_write("KRYFS bicimlendiriliyor...\n");

    uint8_t sector_buf[KRYFS_BLOCK_SIZE];
    memset(sector_buf, 0, KRYFS_BLOCK_SIZE);

    kryfs_superblock_t* sb = (kryfs_superblock_t*)sector_buf;
    sb->magic = KRYFS_MAGIC;
    sb->total_sectors = 2048;
    sb->inode_count = 16;
    sb->block_size = KRYFS_BLOCK_SIZE;
    
    const char* name = "KryonVolume";
    for (int i = 0; i < 31 && name[i] != '\0'; i++) {
        sb->volume_name[i] = name[i];
    }

    // Süperbloğu 0. sektöre yaz
    ata_write_sector(KRYFS_SUPERBLOCK_SECTOR, sector_buf);

    // --- TEST DOSYASI OLUŞTURMA (Inode 0) ---
    memset(sector_buf, 0, KRYFS_BLOCK_SIZE);
    kryfs_inode_t* inodes = (kryfs_inode_t*)sector_buf;
    
    inodes[0].inode_id = 1;
    const char* test_filename = "test.txt";
    for (int i = 0; i < 31 && test_filename[i] != '\0'; i++) {
        inodes[0].filename[i] = test_filename[i];
    }
    inodes[0].size = 13; // "KryonOS Rocks!" yazısının uzunluğu
    inodes[0].first_block = 2; // Verinin yazılacağı blok
    inodes[0].is_used = 1;

    // Inode tablosunu 1. sektöre yaz
    ata_write_sector(1, sector_buf);

    // --- DOSYA İÇERİĞİNİ YAZMA (2. Sektör) ---
    memset(sector_buf, 0, KRYFS_BLOCK_SIZE);
    const char* file_content = "KryonOS Rocks!";
    for (int i = 0; i < 13; i++) {
        sector_buf[i] = file_content[i];
    }
    ata_write_sector(2, sector_buf);

    serial_write("KRYFS bicimlendirme tamamlandi, test dosyasi diske yazildi.\n");
}

void kryfs_init(void) {
    serial_write("KRYFS baslatiliyor...\n");

    uint8_t sector_buf[KRYFS_BLOCK_SIZE];
    ata_read_sector(KRYFS_SUPERBLOCK_SECTOR, sector_buf);

    kryfs_superblock_t* sb = (kryfs_superblock_t*)sector_buf;

    if (sb->magic != KRYFS_MAGIC) {
        serial_write("UYARI: Gecersiz KRYFS imzi bulundu, dosya sistemi bicimlendiriliyor...\n");
        kryfs_format();
    } else {
        serial_write("KRYFS superblok basariyla dogrulandi!\n");
    }
}

void* kryfs_read_file(const char* filename, uint32_t* out_size) {
    if (!filename) {
        if (out_size) *out_size = 0;
        return 0;
    }

    uint8_t sector_buf[KRYFS_BLOCK_SIZE];
    ata_read_sector(0, sector_buf);
    kryfs_superblock_t* sb = (kryfs_superblock_t*)sector_buf;

    if (sb->magic != KRYFS_MAGIC) {
        if (out_size) *out_size = 0;
        return 0;
    }

    uint32_t inode_sector_start = 1;
    uint32_t inodes_per_sector = KRYFS_BLOCK_SIZE / sizeof(kryfs_inode_t);
    uint32_t total_inode_sectors = (sb->inode_count + inodes_per_sector - 1) / inodes_per_sector;

    kryfs_inode_t target_inode;
    int found = 0;

    for (uint32_t s = 0; s < total_inode_sectors; s++) {
        ata_read_sector(inode_sector_start + s, sector_buf);
        kryfs_inode_t* inodes = (kryfs_inode_t*)sector_buf;

        for (uint32_t i = 0; i < inodes_per_sector; i++) {
            if (inodes[i].is_used && strcmp(inodes[i].filename, filename) == 0) {
                target_inode = inodes[i];
                found = 1;
                break;
            }
        }
        if (found) break;
    }

    if (!found) {
        serial_write("KRYFS: Dosya bulunamadi!\n");
        if (out_size) *out_size = 0;
        return 0;
    }

    uint32_t file_size = target_inode.size;
    uint32_t current_block = target_inode.first_block;
    uint32_t bytes_read = 0;

    while (bytes_read < file_size && current_block > 0) {
        uint8_t block_buf[KRYFS_BLOCK_SIZE];
        ata_read_sector(current_block, block_buf);

        uint32_t chunk = (file_size - bytes_read > KRYFS_BLOCK_SIZE) ? KRYFS_BLOCK_SIZE : (file_size - bytes_read);
        memcpy(file_read_buffer + bytes_read, block_buf, chunk);

        bytes_read += chunk;
        current_block++;
    }

    if (out_size) *out_size = file_size;
    return file_read_buffer;
}