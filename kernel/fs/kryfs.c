#include <kernel/fs/kryfs.h>
#include <kernel/drivers/storage/ata.h>
#include <kernel/serial.h>

#define KRYFS_SUPERBLOCK_SECTOR 0

void kryfs_format(void) {
    serial_write("KRYFS bicimlendiriliyor...\n");

    uint8_t sector_buf[KRYFS_BLOCK_SIZE];
    for (int i = 0; i < KRYFS_BLOCK_SIZE; i++) {
        sector_buf[i] = 0;
    }

    kryfs_superblock_t* sb = (kryfs_superblock_t*)sector_buf;
    sb->magic = KRYFS_MAGIC;
    sb->total_sectors = 2048; // Örn: 1MB disk imajı için
    sb->inode_count = 16;
    sb->block_size = KRYFS_BLOCK_SIZE;
    
    // Volume adını kopyala
    const char* name = "KryonVolume";
    int i = 0;
    while (name[i] != '\0' && i < 31) {
        sb->volume_name[i] = name[i];
        i++;
    }
    sb->volume_name[i] = '\0';

    // Süperbloğu 0. sektöre yaz
    ata_write_sector(KRYFS_SUPERBLOCK_SECTOR, sector_buf);
    serial_write("KRYFS bicimlendirme tamamlandi, superblok diske yazildi.\n");
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