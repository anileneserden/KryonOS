#include <kernel/fs/fat32.h>
#include <kernel/drivers/storage/ata.h>
#include <kernel/mem/heap.h>
#include <kernel/serial.h>
#include <kernel/string.h>

static fat32_bpb_t bpb;
static uint8_t current_drive = 1; // Slave by default (Drive 1)
static uint32_t fat_partition_lba = 0;
static uint32_t fat_start_sector = 0;
static uint32_t data_start_sector = 0;
static bool is_fat32_initialized = false;

// Convert a cluster number to an LBA sector address
static uint32_t cluster_to_lba(uint32_t cluster) {
    return data_start_sector + ((cluster - 2) * bpb.sectors_per_cluster);
}

// Convert the 8.3 filename format to the VFS standard ("TEST    TXT" -> "TEST.TXT")
static void format_fat_name(const uint8_t* fat_name, char* out_name) {
    int pos = 0;
    
    // Get the filename portion (the first 8 bytes)
    for (int i = 0; i < 8; i++) {
        if (fat_name[i] != ' ') {
            out_name[pos++] = fat_name[i];
        }
    }
    
    // Check for an extension (the last 3 bytes)
    if (fat_name[8] != ' ') {
        out_name[pos++] = '.';
        for (int i = 8; i < 11; i++) {
            if (fat_name[i] != ' ') {
                out_name[pos++] = fat_name[i];
            }
        }
    }
    out_name[pos] = '\0';
}

// Read a FAT entry (cluster chain value)
static uint32_t get_fat_entry(uint32_t cluster) {
    if (!is_fat32_initialized) return 0;

    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_start_sector + (fat_offset / 512);
    uint32_t ent_offset = fat_offset % 512;

    uint8_t sector_buffer[512];
    ata_read_sectors(current_drive, fat_sector, 1, sector_buffer);

    uint32_t table_value = *(uint32_t*)&sector_buffer[ent_offset];
    return table_value & 0x0FFFFFFF; // Mask top 4 bits for FAT32
}

// Set a FAT entry (cluster chain value) for write operations
static void set_fat_entry(uint32_t cluster, uint32_t value) {
    if (!is_fat32_initialized) return;

    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_start_sector + (fat_offset / 512);
    uint32_t ent_offset = fat_offset % 512;

    uint8_t sector_buffer[512];
    ata_read_sectors(current_drive, fat_sector, 1, sector_buffer);

    // Preserve upper 4 bits while updating value
    uint32_t* entry = (uint32_t*)&sector_buffer[ent_offset];
    *entry = (*entry & 0xF0000000) | (value & 0x0FFFFFFF);

    // Write to all FAT copies (typically 2 FAT tables)
    for (int i = 0; i < bpb.num_fats; i++) {
        uint32_t target_sector = fat_sector + (i * bpb.table_size_32);
        ata_write_sectors(current_drive, target_sector, 1, sector_buffer);
    }
}

// Find and allocate a free cluster from the FAT table
static uint32_t allocate_cluster(void) {
    if (!is_fat32_initialized) return 0;

    // FAT32 clusters typically start from cluster 2
    uint32_t total_sectors = bpb.total_sectors_32 != 0 ? bpb.total_sectors_32 : bpb.total_sectors_16;
    uint32_t fat_size = bpb.table_size_32;
    uint32_t total_clusters = (fat_size * 512) / 4;

    for (uint32_t cluster = 2; cluster < total_clusters; cluster++) {
        if (get_fat_entry(cluster) == 0x0000000) {
            set_fat_entry(cluster, 0x0FFFFFFF);
            return cluster;
        }
    }

    serial_write("FAT32 Error: Disk is full, no free clusters available!\n");
    return 0;
}

// VFS: file writing (creates or overwrites a file)
static bool fat32_write_file(const char* path, const void* data, uint32_t size) {
    if (!is_fat32_initialized || !data) return false;

    // 1. Allocate a cluster for the file data
    uint32_t first_cluster = allocate_cluster();
    if (first_cluster == 0) return false;

    // 2. Write data to the data region sectors
    uint32_t file_lba = cluster_to_lba(first_cluster);
    uint32_t sectors_to_write = (size + 511) / 512;
    
    // Write sectors chunk by chunk or all at once depending on ATA driver support
    // Assuming ata_write_sectors can handle multiple sectors or we write 512-byte blocks:
    const uint8_t* byte_data = (const uint8_t*)data;
    for (uint32_t i = 0; i < sectors_to_write; i++) {
        uint8_t sector_buf[512] = {0};
        uint32_t bytes_to_copy = (size - (i * 512) > 512) ? 512 : (size - (i * 512));
        memcpy(sector_buf, byte_data + (i * 512), bytes_to_copy);
        ata_write_sectors(current_drive, file_lba + i, 1, sector_buf);
    }

    // 3. Read root directory to find an empty slot for the new file entry
    uint32_t root_lba = cluster_to_lba(bpb.root_cluster);
    uint8_t root_buffer[512];
    ata_read_sectors(current_drive, root_lba, 1, root_buffer);

    fat32_dir_entry_t* entries = (fat32_dir_entry_t*)root_buffer;
    int max_entries = 512 / sizeof(fat32_dir_entry_t);
    int free_index = -1;

    for (int i = 0; i < max_entries; i++) {
        if (entries[i].name[0] == 0x00 || entries[i].name[0] == 0xE5) {
            free_index = i;
            break;
        }
    }

    if (free_index == -1) {
        serial_write("FAT32 Error: Root directory is full!\n");
        return false;
    }

    // 4. Prepare and fill the 8.3 filename in the directory entry
    memset(entries[free_index].name, ' ', 11);
    
    // Simple parser for filename and extension (e.g., "TEST.TXT" -> name: "TEST    ", ext: "TXT")
    int name_len = 0;
    while (path[name_len] != '\0' && path[name_len] != '.' && name_len < 8) {
        entries[free_index].name[name_len] = path[name_len];
        name_len++;
    }

    if (path[name_len] == '.') {
        int ext_len = 0;
        name_len++; // skip dot
        while (path[name_len + ext_len] != '\0' && ext_len < 3) {
            entries[free_index].name[8 + ext_len] = path[name_len + ext_len];
            ext_len++;
        }
    }

    // Fill file metadata
    entries[free_index].attr = 0x20; // Archive file
    entries[free_index].first_cluster_high = (uint16_t)((first_cluster >> 16) & 0xFFFF);
    entries[free_index].first_cluster_low = (uint16_t)(first_cluster & 0xFFFF);
    entries[free_index].file_size = size;

    // 5. Write back the updated root directory sector to disk
    ata_write_sectors(current_drive, root_lba, 1, root_buffer);

    serial_write("FAT32: File written successfully.\n");
    return true;
}

// Initialize the FAT32 partition (Drive 0: Master, Drive 1: Slave)
bool fat32_init_disk(uint8_t drive, uint32_t lba_start) {
    current_drive = drive;
    fat_partition_lba = lba_start;
    
    // Read the boot sector (BPB) (512 bytes)
    uint8_t sector_buffer[512];
    ata_read_sectors(current_drive, fat_partition_lba, 1, sector_buffer);
    
    memcpy(&bpb, sector_buffer, sizeof(fat32_bpb_t));

    // Validate FAT32
    if (bpb.bytes_per_sector != 512 || bpb.sectors_per_cluster == 0) {
        serial_write("FAT32 Error: Invalid BPB or unsupported sector size!\n");
        return false;
    }

    fat_start_sector = fat_partition_lba + bpb.reserved_sector_count;
    uint32_t fat_size = bpb.table_size_32 * bpb.num_fats;
    data_start_sector = fat_start_sector + fat_size;

    is_fat32_initialized = true;
    serial_write("FAT32: Driver loaded successfully.\n");
    return true;
}

// VFS: file reading
static void* fat32_read_file(const char* path, uint32_t* out_size) {
    if (!is_fat32_initialized) {
        if (out_size) *out_size = 0;
        return NULL;
    }

    // Read the root directory sector
    uint32_t root_lba = cluster_to_lba(bpb.root_cluster);
    uint8_t buffer[512];
    ata_read_sectors(current_drive, root_lba, 1, buffer);

    fat32_dir_entry_t* entries = (fat32_dir_entry_t*)buffer;
    int max_entries = 512 / sizeof(fat32_dir_entry_t);

    for (int i = 0; i < max_entries; i++) {
        if (entries[i].name[0] == 0x00) break; // No more entries
        if (entries[i].name[0] == 0xE5) continue; // Deleted file
        if (entries[i].attr & 0x10) continue; // Skip directories

        char formatted_name[13];
        format_fat_name(entries[i].name, formatted_name);

        if (strcmp(formatted_name, path) == 0) {
            uint32_t file_size = entries[i].file_size;
            uint32_t first_cluster = ((uint32_t)entries[i].first_cluster_high << 16) | entries[i].first_cluster_low;
            
            // Load the file into heap memory
            void* file_data = kmalloc(file_size);
            if (!file_data) {
                if (out_size) *out_size = 0;
                return NULL;
            }

            uint32_t file_lba = cluster_to_lba(first_cluster);
            uint32_t sectors_to_read = (file_size + 511) / 512;
            
            ata_read_sectors(current_drive, file_lba, sectors_to_read, (uint8_t*)file_data);

            if (out_size) *out_size = file_size;
            return file_data;
        }
    }

    if (out_size) *out_size = 0;
    return NULL;
}

// VFS: directory listing (console output)
static void fat32_list_dir(void) {
    if (!is_fat32_initialized) return;

    uint32_t root_lba = cluster_to_lba(bpb.root_cluster);
    uint8_t buffer[512];
    ata_read_sectors(current_drive, root_lba, 1, buffer);

    fat32_dir_entry_t* entries = (fat32_dir_entry_t*)buffer;
    int max_entries = 512 / sizeof(fat32_dir_entry_t);

    for (int i = 0; i < max_entries; i++) {
        if (entries[i].name[0] == 0x00) break;
        if (entries[i].name[0] == 0xE5) continue;

        char formatted_name[13];
        format_fat_name(entries[i].name, formatted_name);

        serial_write(formatted_name);
        if (entries[i].attr & 0x10) {
            serial_write(" <DIR>\n");
        } else {
            serial_write("\n");
        }
    }
}

// VFS: populate the directory files in the VFS structure
static int fat32_get_dir_files(const char* path, vfs_file_info_t* out_list, int max_count) {
    (void)path;
    if (!is_fat32_initialized || !out_list) return 0;

    uint32_t root_lba = cluster_to_lba(bpb.root_cluster);
    uint8_t buffer[512];
    ata_read_sectors(current_drive, root_lba, 1, buffer);

    fat32_dir_entry_t* entries = (fat32_dir_entry_t*)buffer;
    int max_entries = 512 / sizeof(fat32_dir_entry_t);
    int count = 0;

    for (int i = 0; i < max_entries && count < max_count; i++) {
        if (entries[i].name[0] == 0x00) break;
        if (entries[i].name[0] == 0xE5) continue;

        format_fat_name(entries[i].name, out_list[count].name);
        out_list[count].size = entries[i].file_size;
        out_list[count].is_directory = (entries[i].attr & 0x10) != 0;
        
        count++;
    }

    return count;
}

// VFS fs_driver_t interface binding
fs_driver_t fat32_get_driver(void) {
    fs_driver_t driver;
    driver.read_file = fat32_read_file;
    driver.write_file = fat32_write_file;
    driver.list_dir = fat32_list_dir;
    driver.get_dir_files = fat32_get_dir_files;
    return driver;
}