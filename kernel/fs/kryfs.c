#include <kernel/fs/kryfs.h>
#include <kernel/fs/vfs.h>
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
        serial_write("UYARI: Gecersiz KRYFS imzasi bulundu, dosya sistemi bicimlendiriliyor...\n");
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

void kryfs_list_files(void) {
    uint8_t sector_buf[KRYFS_BLOCK_SIZE];
    ata_read_sector(0, sector_buf);
    kryfs_superblock_t* sb = (kryfs_superblock_t*)sector_buf;

    if (sb->magic != KRYFS_MAGIC) {
        serial_write("KRYFS: Gecersiz superblok!\n");
        return;
    }

    serial_write("\n========================================\n");
    serial_write(" Sürücü C:\\ [ ");
    for (int i = 0; i < 31 && sb->volume_name[i] != '\0'; i++) {
        char c[2] = { sb->volume_name[i], '\0' };
        serial_write(c);
    }
    serial_write(" ]\n");
    serial_write("========================================\n");

    uint32_t inode_sector_start = 1;
    uint32_t inodes_per_sector = KRYFS_BLOCK_SIZE / sizeof(kryfs_inode_t);
    uint32_t total_inode_sectors = (sb->inode_count + inodes_per_sector - 1) / inodes_per_sector;

    int file_count = 0;
    for (uint32_t s = 0; s < total_inode_sectors; s++) {
        ata_read_sector(inode_sector_start + s, sector_buf);
        kryfs_inode_t* inodes = (kryfs_inode_t*)sector_buf;

        for (uint32_t i = 0; i < inodes_per_sector; i++) {
            uint32_t current_idx = s * inodes_per_sector + i;
            if (current_idx >= sb->inode_count) break;

            if (inodes[i].is_used) {
                file_count++;
                
                // Dosya adını veya yolunu geçici bir tampona güvenle al
                char name_buf[33];
                int name_len = 0;
                for (int j = 0; j < 32 && inodes[i].filename[j] != '\0'; j++) {
                    name_buf[j] = inodes[i].filename[j];
                    name_len = j + 1;
                }
                name_buf[name_len] = '\0';

                // 1. Dizin derinliğini hesapla (yoldaki '/' sayısı)
                int depth = 0;
                for (int j = 0; j < name_len; j++) {
                    if (name_buf[j] == '/') {
                        depth++;
                    }
                }

                // Eğer son karakter '/' ise (klasörün kendi yolu), çizimde fazladan sayılmasın diye derinliği dengeleyebiliriz
                // Veya her seviye için uygun girintiyi bırakıyoruz:
                // Kök seviyedekiler için depth = 0 (veya klasör içi için 1 vb.)
                
                // 2. Derinliğe göre boşluk ve ağaç dallarını bas
                // Ana dizindekiler için 2 boşluk + "|--- ", alt dizindekiler için hiyerarşik boşluk + "|-- "
                if (depth == 0 || (depth == 1 && name_buf[name_len - 1] == '/')) {
                    // Kök seviye dosya veya klasör
                    serial_write("  |--- ");
                } else {
                    // Alt seviyeler için girinti yap
                    for (int d = 0; d < depth; d++) {
                        serial_write("    ");
                    }
                    serial_write(" |-- ");
                }

                // 3. İsmi ekrana yazdır (Eğer klasörse sondaki '/' işaretini tree görünümü için temizleyebilir veya koruyabilirsin)
                for (int j = 0; j < name_len; j++) {
                    char c[2] = { name_buf[j], '\0' };
                    serial_write(c);
                }
                
                serial_write("\n");
            }
        }
    }

    if (file_count == 0) {
        serial_write("  (Dizin bos)\n");
    }
    serial_write("========================================\n\n");
}

// VFS için Sürücü Adaptörü ve Başlatıcı
fs_driver_t kryfs_get_driver(void) {
    fs_driver_t driver;
    driver.read_file = kryfs_read_file;
    driver.list_dir = kryfs_list_files;
    return driver;
}

void kryos_fs_system_init(void) {
    kryfs_init();
    fs_driver_t kryfs_driver = kryfs_get_driver();
    vfs_mount('C', "KryonVolume", kryfs_driver);
}