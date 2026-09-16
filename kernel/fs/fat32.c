#include <kernel/fs/fat32.h>
#include <kernel/drivers/storage/ata.h>
#include <kernel/mem/heap.h>
#include <kernel/serial.h>
#include <kernel/string.h>

static fat32_bpb_t bpb;
static uint8_t current_drive = 1; // Varsayılan olarak Slave (Drive 1)
static uint32_t fat_partition_lba = 0;
static uint32_t fat_start_sector = 0;
static uint32_t data_start_sector = 0;
static bool is_fat32_initialized = false;

// Küme (Cluster) numarasını LBA sektor adresine çevirir
static uint32_t cluster_to_lba(uint32_t cluster) {
    return data_start_sector + ((cluster - 2) * bpb.sectors_per_cluster);
}

// 8.3 dosya adı formatını VFS standart adına dönüştürür ("TEST    TXT" -> "TEST.TXT")
static void format_fat_name(const uint8_t* fat_name, char* out_name) {
    int pos = 0;
    
    // Dosya adı kısmını al (ilk 8 bayt)
    for (int i = 0; i < 8; i++) {
        if (fat_name[i] != ' ') {
            out_name[pos++] = fat_name[i];
        }
    }
    
    // Uzantı var mı kontrol et (son 3 bayt)
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

// FAT32 Bölümünü Başlatır (Drive 0: Master, Drive 1: Slave)
bool fat32_init_disk(uint8_t drive, uint32_t lba_start) {
    current_drive = drive;
    fat_partition_lba = lba_start;
    
    // Boot Sektorunu (BPB) Oku (512 Bayt)
    uint8_t sector_buffer[512];
    ata_read_sectors(current_drive, fat_partition_lba, 1, sector_buffer);
    
    memcpy(&bpb, sector_buffer, sizeof(fat32_bpb_t));

    // FAT32 Doğrulama
    if (bpb.bytes_per_sector != 512 || bpb.sectors_per_cluster == 0) {
        serial_write("FAT32 Hata: Gecersiz BPB veya desteklenmeyen sektor boyutu!\n");
        return false;
    }

    fat_start_sector = fat_partition_lba + bpb.reserved_sector_count;
    uint32_t fat_size = bpb.table_size_32 * bpb.num_fats;
    data_start_sector = fat_start_sector + fat_size;

    is_fat32_initialized = true;
    serial_write("FAT32: Surucu basariyla yuklendi.\n");
    return true;
}

// VFS İçin: Dosya Okuma
static void* fat32_read_file(const char* path, uint32_t* out_size) {
    if (!is_fat32_initialized) {
        if (out_size) *out_size = 0;
        return NULL;
    }

    // Kök Dizin Sektorunu Oku
    uint32_t root_lba = cluster_to_lba(bpb.root_cluster);
    uint8_t buffer[512];
    ata_read_sectors(current_drive, root_lba, 1, buffer);

    fat32_dir_entry_t* entries = (fat32_dir_entry_t*)buffer;
    int max_entries = 512 / sizeof(fat32_dir_entry_t);

    for (int i = 0; i < max_entries; i++) {
        if (entries[i].name[0] == 0x00) break; // Başka girdi yok
        if (entries[i].name[0] == 0xE5) continue; // Silinmiş dosya
        if (entries[i].attr & 0x10) continue; // Dizinleri atla

        char formatted_name[13];
        format_fat_name(entries[i].name, formatted_name);

        if (strcmp(formatted_name, path) == 0) {
            uint32_t file_size = entries[i].file_size;
            uint32_t first_cluster = ((uint32_t)entries[i].first_cluster_high << 16) | entries[i].first_cluster_low;
            
            // Dosyayı Heap belleğine yükle
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

// VFS İçin: Dizin Listeleme (Konsol Çıktısı)
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

// VFS İçin: Dizin Dosyalarını VFS Yapısına Doldurma
static int fat32_get_dir_files(const char* path, vfs_file_info_t* out_list, int max_count) {
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

// VFS 'fs_driver_t' Arayüz Bağlantısı
fs_driver_t fat32_get_driver(void) {
    fs_driver_t driver;
    driver.read_file = fat32_read_file;
    driver.list_dir = fat32_list_dir;
    driver.get_dir_files = fat32_get_dir_files;
    return driver;
}