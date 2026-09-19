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

    for (uint32_t i = 0; i < inodes_per_sector; i++) {
        if (inodes[i].is_used) {
            serial_write("[KRYFS] Inode taranıyor: ");
            serial_write(inodes[i].filename);
            serial_write("\n");

            if (strcmp(inodes[i].filename, filename) == 0) {
                // Dosya bulundu, dışarıdaki yapıya kopyala
                memcpy(out_inode, &inodes[i], sizeof(kryfs_inode_t));
                return 0; // Başarılı
            }
        }
    }

    serial_write("[KRYFS] Aranan dosya bulunamadi: ");
    serial_write((char*)filename);
    serial_write("\n");
    return -1; // Bulunamadı
}