#include <kernel/fs/kryfs.h>
#include <kernel/fs/vfs.h>
#include <kernel/drivers/storage/ata.h>
#include <kernel/serial.h>
#include <kernel/string.h>
#include <kernel/mem/heap.h>

#define KRYFS_SUPERBLOCK_SECTOR 0
#define KRYFS_DRIVE 0 // Master Drive (Drive 0)

void kryfs_format(void) {
    serial_write("KRYFS formatting (adding default directories)...\n");

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

    ata_write_sector(KRYFS_DRIVE, KRYFS_SUPERBLOCK_SECTOR, sector_buf);

    // --- INODE TABLE (Default directories and files) ---
    memset(sector_buf, 0, KRYFS_BLOCK_SIZE);
    kryfs_inode_t* inodes = (kryfs_inode_t*)sector_buf;

    // 0. Inode: Users/ directory
    inodes[0].inode_id = 1;
    strcpy(inodes[0].filename, "Users/");
    inodes[0].size = 0;
    inodes[0].first_block = 0;
    inodes[0].is_used = 1;
    inodes[0].is_directory = 1;

    // 1. Inode: Users/anil/ directory
    inodes[1].inode_id = 2;
    strcpy(inodes[1].filename, "Users/anil/");
    inodes[1].size = 0;
    inodes[1].first_block = 0;
    inodes[1].is_used = 1;
    inodes[1].is_directory = 1;

    // 2. Inode: Users/anil/Desktop/ directory
    inodes[2].inode_id = 3;
    strcpy(inodes[2].filename, "Users/anil/Desktop/");
    inodes[2].size = 0;
    inodes[2].first_block = 0;
    inodes[2].is_used = 1;
    inodes[2].is_directory = 1;

    // 3. Inode: Users/anil/Music/ directory
    inodes[3].inode_id = 4;
    strcpy(inodes[3].filename, "Users/anil/Music/");
    inodes[3].size = 0;
    inodes[3].first_block = 0;
    inodes[3].is_used = 1;
    inodes[3].is_directory = 1;

    // 4. Inode: Test file (inside Desktop)
    inodes[4].inode_id = 5;
    strcpy(inodes[4].filename, "Users/anil/Desktop/test.txt");
    inodes[4].size = 13;
    inodes[4].first_block = 5; // Block containing the data
    inodes[4].is_used = 1;
    inodes[4].is_directory = 0;

    // Superblock'un arkasına yazılıyor (Sektör 0 içerisindeki kalan alan)
    ata_write_sector(KRYFS_DRIVE, 1, sector_buf);

    // --- WRITE TEST FILE CONTENT (sector 5) ---
    memset(sector_buf, 0, KRYFS_BLOCK_SIZE);
    const char* file_content = "KryonOS Rocks!";
    memcpy(sector_buf, file_content, 13);
    ata_write_sector(KRYFS_DRIVE, 5, sector_buf);

    serial_write("KRYFS formatting complete, directory tree created.\n");
}

void kryfs_init(void) {
    serial_write("KRYFS starting...\n");

    uint8_t sector_buf[KRYFS_BLOCK_SIZE];
    ata_read_sector(KRYFS_DRIVE, KRYFS_SUPERBLOCK_SECTOR, sector_buf);

    kryfs_superblock_t* sb = (kryfs_superblock_t*)sector_buf;

    if (sb->magic != KRYFS_MAGIC) {
        serial_write("UYARI: Gecersiz KRYFS imzasi bulundu!\n");
    } else {
        serial_write("KRYFS superblock verified successfully!\n");
    }
}

void* kryfs_read_file(const char* filename, uint32_t* out_size) {
    if (!filename) {
        if (out_size) *out_size = 0;
        return 0;
    }

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

    kryfs_inode_t target_inode;
    int found = 0;

    uint32_t inode_size = sizeof(kryfs_inode_t);
    uint32_t base_offset = sizeof(kryfs_superblock_t);

    for (uint32_t i = 0; i < sb->inode_count; i++) {
        uint32_t byte_offset = base_offset + (i * inode_size);
        uint32_t sector_num = byte_offset / KRYFS_BLOCK_SIZE;
        uint32_t sector_offset = byte_offset % KRYFS_BLOCK_SIZE;

        uint8_t current_sector_buf[KRYFS_BLOCK_SIZE];
        ata_read_sector(KRYFS_DRIVE, sector_num, current_sector_buf);

        kryfs_inode_t* inode = (kryfs_inode_t*)(current_sector_buf + sector_offset);

        if (inode->is_used && strcmp(inode->filename, filename) == 0) {
            target_inode = *inode;
            found = 1;
            break;
        }
    }

    if (!found) {
        serial_write("KRYFS: File not found -> ");
        serial_write(filename);
        serial_write("\n");
        if (out_size) *out_size = 0;
        return 0;
    }

    uint32_t file_size = target_inode.size;

    uint8_t* dynamic_buffer = (uint8_t*)kmalloc(file_size);
    if (!dynamic_buffer) {
        serial_write("KRYFS: Insufficient memory (kmalloc failed)!\n");
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
        serial_write("KRYFS: Invalid superblock!\n");
        return;
    }

    serial_write("\n========================================\n");
    serial_write(" Drive C:\\ [ ");
    for (int i = 0; i < 31 && sb->volume_name[i] != '\0'; i++) {
        char c[2] = { sb->volume_name[i], '\0' };
        serial_write(c);
    }
    serial_write(" ]\n");
    serial_write("========================================\n");

    uint32_t inode_size = sizeof(kryfs_inode_t);
    uint32_t base_offset = sizeof(kryfs_superblock_t);
    int file_count = 0;

    // Bayt offset tabanlı tarama (Çöp / boş inode'lar filtrelendi)
    for (uint32_t i = 0; i < sb->inode_count; i++) {
        uint32_t byte_offset = base_offset + (i * inode_size);
        uint32_t sector_num = byte_offset / KRYFS_BLOCK_SIZE;
        uint32_t sector_offset = byte_offset % KRYFS_BLOCK_SIZE;

        uint8_t current_sector_buf[KRYFS_BLOCK_SIZE];
        ata_read_sector(KRYFS_DRIVE, sector_num, current_sector_buf);

        kryfs_inode_t* inode = (kryfs_inode_t*)(current_sector_buf + sector_offset);

        // Inode kullanımda mi VE dosya adı boş değil mi?
        if (inode->is_used && inode->filename[0] != '\0' && inode->filename[0] != ' ') {
            file_count++;
            
            char name_buf[33];
            int name_len = 0;
            for (int j = 0; j < 32 && inode->filename[j] != '\0'; j++) {
                name_buf[j] = inode->filename[j];
                name_len = j + 1;
            }
            name_buf[name_len] = '\0';

            // Hiyerarşik görünüm
            serial_write("  |--- ");
            serial_write(name_buf);
            if (inode->is_directory) {
                serial_write(" <DIR>");
            }
            serial_write("\n");
        }
    }

    if (file_count == 0) {
        serial_write("  (Directory empty)\n");
    }
    serial_write("========================================\n\n");
}

int kryfs_get_dir_files(const char* path, vfs_file_info_t* out_list, int max_count) {
    uint8_t sector_buf[KRYFS_BLOCK_SIZE];
    ata_read_sector(KRYFS_DRIVE, 0, sector_buf);
    kryfs_superblock_t* sb = (kryfs_superblock_t*)sector_buf;

    if (sb->magic != KRYFS_MAGIC) return 0;

    int target_len = 0;
    while (path[target_len] != '\0') target_len++;

    uint32_t inode_size = sizeof(kryfs_inode_t);
    uint32_t base_offset = sizeof(kryfs_superblock_t);
    int count = 0;

    // Bayt offset tabanlı arama yapılıyor (eski sabit sektör 1 mantığı kaldırıldı)
    for (uint32_t i = 0; i < sb->inode_count && count < max_count; i++) {
        uint32_t byte_offset = base_offset + (i * inode_size);
        uint32_t sector_num = byte_offset / KRYFS_BLOCK_SIZE;
        uint32_t sector_offset = byte_offset % KRYFS_BLOCK_SIZE;

        uint8_t current_sector_buf[KRYFS_BLOCK_SIZE];
        ata_read_sector(KRYFS_DRIVE, sector_num, current_sector_buf);

        kryfs_inode_t* inode = (kryfs_inode_t*)(current_sector_buf + sector_offset);

        if (inode->is_used && inode->filename[0] != '\0') {
            int match = 1;
            for (int j = 0; j < target_len; j++) {
                if (inode->filename[j] != path[j]) {
                    match = 0;
                    break;
                }
            }

            if (match) {
                int len = 0;
                while (inode->filename[len] != '\0') len++;

                int internal_slashes = 0;
                int end_limit = (inode->filename[len - 1] == '/') ? len - 1 : len;
                
                for (int j = target_len; j < end_limit; j++) {
                    if (inode->filename[j] == '/') {
                        internal_slashes++;
                    }
                }

                if (internal_slashes == 0 && len > target_len) {
                    const char* base_name = &inode->filename[target_len];
                    int k = 0;
                    while (base_name[k] != '\0' && k < 31) {
                        out_list[count].name[k] = base_name[k];
                        k++;
                    }
                    out_list[count].name[k] = '\0';
                    out_list[count].size = inode->size;
                    out_list[count].is_directory = inode->is_directory;
                    count++;
                }
            }
        }
    }
    return count;
}

// VFS driver adapter and initializer
fs_driver_t kryfs_get_driver(void) {
    fs_driver_t driver;
    driver.read_file = kryfs_read_file;
    driver.list_dir = kryfs_list_files;
    driver.get_dir_files = kryfs_get_dir_files;
    return driver;
}

void kryos_fs_system_init(void) {
    kryfs_init();
    fs_driver_t kryfs_driver = kryfs_get_driver();
    vfs_mount('C', "KryonVolume", kryfs_driver);
}