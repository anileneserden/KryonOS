#include <kernel/fs/kryfs.h>
#include <kernel/fs/vfs.h>
#include <kernel/drivers/storage/ata.h>
#include <kernel/serial.h>
#include <kernel/string.h>
#include <kernel/mem/heap.h>

#define KRYFS_SUPERBLOCK_SECTOR 0
#define KRYFS_DRIVE 0 // Master Drive (Drive 0)

void kryfs_format(void) {
    serial_write("KRYFS bicimlendiriliyor (Varsayilan dizinler ekleniyor)...\n");

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
    // DRIVE 0 PARAMETRESİ EKLENDİ
    ata_write_sector(KRYFS_DRIVE, KRYFS_SUPERBLOCK_SECTOR, sector_buf);

    // --- INODE TABLOSU (Varsayılan Dizinler ve Dosyalar) ---
    memset(sector_buf, 0, KRYFS_BLOCK_SIZE);
    kryfs_inode_t* inodes = (kryfs_inode_t*)sector_buf;

    // 0. Inode: Users/ klasörü
    inodes[0].inode_id = 1;
    strcpy(inodes[0].filename, "Users/");
    inodes[0].size = 0;
    inodes[0].first_block = 0;
    inodes[0].is_used = 1;
    inodes[0].is_directory = 1;

    // 1. Inode: Users/anil/ klasörü
    inodes[1].inode_id = 2;
    strcpy(inodes[1].filename, "Users/anil/");
    inodes[1].size = 0;
    inodes[1].first_block = 0;
    inodes[1].is_used = 1;
    inodes[1].is_directory = 1;

    // 2. Inode: Users/anil/Desktop/ klasörü
    inodes[2].inode_id = 3;
    strcpy(inodes[2].filename, "Users/anil/Desktop/");
    inodes[2].size = 0;
    inodes[2].first_block = 0;
    inodes[2].is_used = 1;
    inodes[2].is_directory = 1;

    // 3. Inode: Users/anil/Music/ klasörü
    inodes[3].inode_id = 4;
    strcpy(inodes[3].filename, "Users/anil/Music/");
    inodes[3].size = 0;
    inodes[3].first_block = 0;
    inodes[3].is_used = 1;
    inodes[3].is_directory = 1;

    // 4. Inode: Test Dosyası (Desktop içinde)
    inodes[4].inode_id = 5;
    strcpy(inodes[4].filename, "Users/anil/Desktop/test.txt");
    inodes[4].size = 13;
    inodes[4].first_block = 5; // Verinin durduğu blok
    inodes[4].is_used = 1;
    inodes[4].is_directory = 0;

    // Inode tablosunu 1. sektöre yaz (DRIVE 0)
    ata_write_sector(KRYFS_DRIVE, 1, sector_buf);

    // --- TEST DOSYASI İÇERİĞİNİ YAZMA (5. Sektör) ---
    memset(sector_buf, 0, KRYFS_BLOCK_SIZE);
    const char* file_content = "KryonOS Rocks!";
    memcpy(sector_buf, file_content, 13);
    ata_write_sector(KRYFS_DRIVE, 5, sector_buf);

    serial_write("KRYFS bicimlendirme tamamlandi, dizin agaci olusturuldu.\n");
}

void kryfs_init(void) {
    serial_write("KRYFS baslatiliyor...\n");

    uint8_t sector_buf[KRYFS_BLOCK_SIZE];
    ata_read_sector(KRYFS_DRIVE, KRYFS_SUPERBLOCK_SECTOR, sector_buf);

    kryfs_superblock_t* sb = (kryfs_superblock_t*)sector_buf;

    if (sb->magic != KRYFS_MAGIC) {
        serial_write("UYARI: Gecersiz KRYFS imzasi bulundu!\n");
        // İsteğe bağlı olarak burada sadece hata dönebilirsin veya 
        // host'tan imaj gelmediyse format atabilirsin.
    } else {
        serial_write("KRYFS superblok basariyla dogrulandi!\n");
    }
}

void* kryfs_read_file(const char* filename, uint32_t* out_size) {
    if (!filename) {
        if (out_size) *out_size = 0;
        return 0;
    }

    // Sürücü harfini (örn: C:/) yoldan arındır
    if (filename[1] == ':' && (filename[2] == '/' || filename[2] == '\\')) {
        filename += 3;
    }

    uint8_t sector_buf[KRYFS_BLOCK_SIZE];
    ata_read_sector(KRYFS_DRIVE, 0, sector_buf);
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
        ata_read_sector(KRYFS_DRIVE, inode_sector_start + s, sector_buf);
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
        serial_write("KRYFS: Dosya bulunamadi -> ");
        serial_write(filename);
        serial_write("\n");
        if (out_size) *out_size = 0;
        return 0;
    }

    serial_write("[KRYFS DEBUG] Dosya bulundu! Boyut: ");
    serial_write_dec(target_inode.size);
    serial_write(" bayt, Ilk Blok: ");
    serial_write_dec(target_inode.first_block);
    serial_write("\n");

    uint32_t file_size = target_inode.size;

    uint8_t* dynamic_buffer = (uint8_t*)kmalloc(file_size);
    if (!dynamic_buffer) {
        serial_write("KRYFS: Yetersiz bellek (kmalloc basarisiz)!\n");
        if (out_size) *out_size = 0;
        return 0;
    }

    uint32_t current_block = target_inode.first_block;
    uint32_t bytes_read = 0;

    while (bytes_read < file_size && current_block > 0) {
        uint8_t block_buf[KRYFS_BLOCK_SIZE];
        ata_read_sector(KRYFS_DRIVE, current_block, block_buf);

        uint32_t chunk = (file_size - bytes_read > KRYFS_BLOCK_SIZE) ? KRYFS_BLOCK_SIZE : (file_size - bytes_read);
        memcpy(dynamic_buffer + bytes_read, block_buf, chunk);

        bytes_read += chunk;
        current_block++;
    }

    if (out_size) *out_size = file_size;
    return dynamic_buffer;
}

void kryfs_list_files(void) {
    uint8_t sector_buf[KRYFS_BLOCK_SIZE];
    ata_read_sector(KRYFS_DRIVE, KRYFS_SUPERBLOCK_SECTOR, sector_buf);
    kryfs_superblock_t* sb = (kryfs_superblock_t*)sector_buf;

    if (sb->magic != KRYFS_MAGIC) {
        serial_write("KRYFS: Gecersiz superblok!\n");
        return;
    }

    serial_write("\n========================================\n");
    serial_write(" Surucu C:\\ [ ");
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
        ata_read_sector(KRYFS_DRIVE, inode_sector_start + s, sector_buf);
        kryfs_inode_t* inodes = (kryfs_inode_t*)sector_buf;

        for (uint32_t i = 0; i < inodes_per_sector; i++) {
            uint32_t current_idx = s * inodes_per_sector + i;
            if (current_idx >= sb->inode_count) break;

            if (inodes[i].is_used) {
                file_count++;
                
                char name_buf[33];
                int name_len = 0;
                for (int j = 0; j < 32 && inodes[i].filename[j] != '\0'; j++) {
                    name_buf[j] = inodes[i].filename[j];
                    name_len = j + 1;
                }
                name_buf[name_len] = '\0';

                // Basit ve şık hiyerarşik tree görünümü
                serial_write("  |--- ");
                serial_write(name_buf);
                if (inodes[i].is_directory) {
                    serial_write(" <DIR>");
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

int kryfs_get_dir_files(const char* path, vfs_file_info_t* out_list, int max_count) {
    uint8_t sector_buf[KRYFS_BLOCK_SIZE];
    ata_read_sector(KRYFS_DRIVE, 0, sector_buf);
    kryfs_superblock_t* sb = (kryfs_superblock_t*)sector_buf;

    if (sb->magic != KRYFS_MAGIC) return 0;

    uint32_t inode_sector_start = 1;
    uint32_t inodes_per_sector = KRYFS_BLOCK_SIZE / sizeof(kryfs_inode_t);
    uint32_t total_inode_sectors = (sb->inode_count + inodes_per_sector - 1) / inodes_per_sector;

    int target_len = 0;
    while (path[target_len] != '\0') target_len++;

    int count = 0;
    for (uint32_t s = 0; s < total_inode_sectors && count < max_count; s++) {
        ata_read_sector(KRYFS_DRIVE, inode_sector_start + s, sector_buf);
        kryfs_inode_t* inodes = (kryfs_inode_t*)sector_buf;

        for (uint32_t i = 0; i < inodes_per_sector && count < max_count; i++) {
            uint32_t current_idx = s * inodes_per_sector + i;
            if (current_idx >= sb->inode_count) break;

            if (inodes[i].is_used) {
                int match = 1;
                for (int j = 0; j < target_len; j++) {
                    if (inodes[i].filename[j] != path[j]) {
                        match = 0;
                        break;
                    }
                }

                if (match) {
                    int len = 0;
                    while (inodes[i].filename[len] != '\0') len++;

                    int internal_slashes = 0;
                    int end_limit = (inodes[i].filename[len - 1] == '/') ? len - 1 : len;
                    
                    for (int j = target_len; j < end_limit; j++) {
                        if (inodes[i].filename[j] == '/') {
                            internal_slashes++;
                        }
                    }

                    if (internal_slashes == 0 && len > target_len) {
                        const char* base_name = &inodes[i].filename[target_len];
                        int k = 0;
                        while (base_name[k] != '\0' && k < 31) {
                            out_list[count].name[k] = base_name[k];
                            k++;
                        }
                        out_list[count].name[k] = '\0';
                        out_list[count].size = inodes[i].size;
                        out_list[count].is_directory = inodes[i].is_directory;
                        count++;
                    }
                }
            }
        }
    }
    return count;
}

// VFS için Sürücü Adaptörü ve Başlatıcı
fs_driver_t kryfs_get_driver(void) {
    fs_driver_t driver;
    driver.read_file = kryfs_read_file;
    driver.list_dir = kryfs_list_files;
    driver.get_dir_files = kryfs_get_dir_files;
    return driver;
}

void kryos_fs_system_init(void) {
    // Doğrudan init çağırıyoruz, otomatik formatlama FUSE imajlarını ezmemeli!
    kryfs_init();
    fs_driver_t kryfs_driver = kryfs_get_driver();
    vfs_mount('C', "KryonVolume", kryfs_driver);
}