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
    serial_write("[KRYFS] -> Veri Bloku Baslangici: ");
    serial_write_num(sb_cache.data_block_start);
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

    // 1. Gelen yoldaki C:\ veya / eklerini temizleyelim (Eğer varsa)
    if (filename[0] == '/' || filename[0] == '\\') {
        filename++;
    } else if (filename[1] == ':' && (filename[2] == '/' || filename[2] == '\\')) {
        filename += 3;
    }

    uint32_t table_lba = sb_cache.inode_table_block;
    uint32_t data_start = sb_cache.data_block_start;

    // Inode tablosunun kapladığı sektör sayısını dinamik olarak hesapla
    uint32_t inode_table_sectors = data_start - table_lba;
    if (inode_table_sectors == 0) inode_table_sectors = 1;

    uint32_t total_buffer_size = inode_table_sectors * 512;
    uint8_t* sector_buf = (uint8_t*)kmalloc(total_buffer_size);
    if (!sector_buf) {
        serial_write("[KRYFS HATA] Inode arama icin bellek tahsisi basarisiz!\n");
        return -1;
    }

    // Tüm inode tablosunu tek seferde oku
    ata_read_sectors(0, table_lba, inode_table_sectors, sector_buf);

    int found = -1;
    uint32_t max_inodes = total_buffer_size / sizeof(kryfs_inode_t);

    for (uint32_t i = 0; i < max_inodes; i++) {
        kryfs_inode_t* inode = (kryfs_inode_t*)(sector_buf + (i * sizeof(kryfs_inode_t)));

        if (inode->is_used) {
            serial_write("[KRYFS] Inode taranıyor: [");
            serial_write(inode->filename);
            serial_write("] | Aranan: [");
            serial_write((char*)filename);
            serial_write("]\n");

            // Tam eşleşme VEYA yolun sonu eşleşmesi kontrolü (Örn: "test.txt" -> "Users/anil/Desktop/test.txt")
            int match = 0;
            if (strcmp(inode->filename, filename) == 0) {
                match = 1;
            } else {
                int in_len = strlen(inode->filename);
                int fn_len = strlen(filename);
                if (in_len > fn_len && 
                    (inode->filename[in_len - fn_len - 1] == '/' || inode->filename[in_len - fn_len - 1] == '\\') &&
                    strcmp(inode->filename + in_len - fn_len, filename) == 0) {
                    match = 1;
                }
            }

            if (match) {
                memcpy(out_inode, inode, sizeof(kryfs_inode_t));
                found = 0;
                break;
            }
        }
    }

    kfree(sector_buf);

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

int kryfs_get_dir_files(const char* rel_path, vfs_file_info_t* out_list, int max_count) {
    if (!kryfs_is_mounted) {
        serial_write("[KRYFS HATA] Dosya sistemi monte edilmemis!\n");
        return 0;
    }

    // Gelen yol başındaki C:\ veya / eklerini temizle
    if (rel_path[0] == '/' || rel_path[0] == '\\') {
        rel_path++;
    } else if (rel_path[1] == ':' && (rel_path[2] == '/' || rel_path[2] == '\\')) {
        rel_path += 3;
    }

    uint32_t table_lba = sb_cache.inode_table_block;
    uint32_t data_start = sb_cache.data_block_start;

    uint32_t inode_table_sectors = data_start - table_lba;
    if (inode_table_sectors == 0) inode_table_sectors = 1;

    uint32_t total_buffer_size = inode_table_sectors * 512;
    uint8_t* sector_buf = (uint8_t*)kmalloc(total_buffer_size);
    if (!sector_buf) {
        serial_write("[KRYFS HATA] Dizin listeleme icin bellek tahsisi basarisiz!\n");
        return 0;
    }

    ata_read_sectors(0, table_lba, inode_table_sectors, sector_buf);

    int count = 0;
    uint32_t max_inodes = total_buffer_size / sizeof(kryfs_inode_t);
    int target_len = strlen(rel_path);

    // Zaten eklenen dosya/klasör isimlerini tekrar eklememek için basit bir kontrol listesi tutabiliriz 
    // (Aynı klasör seviyesindeki unique elemanları bulmak için)
    for (uint32_t i = 0; i < max_inodes && count < max_count; i++) {
        kryfs_inode_t* inode = (kryfs_inode_t*)(sector_buf + (i * sizeof(kryfs_inode_t)));

        if (!inode->is_used) continue;

        const char* in_name = inode->filename;
        int match = 0;
        const char* sub_name = NULL;

        if (target_len == 0) {
            // Kök dizin listeleniyor ("" veya "/")
            // Sadece içinde '/' barındırmayanlar VEYA ilk '/' karakterine kadar olanlar ana dizin elemanıdır.
            // Örn: "test.txt" -> Ana dizin. "Program Files/file.txt" -> Ana dizindeki "Program Files" klasörü.
            sub_name = in_name;
            match = 1;
        } else {
            // Belirli bir alt klasör listeleniyor (Örn: "Program Files")
            // in_name, rel_path ile başlamalı ve hemen ardından bir '/' gelmeli.
            if (strncmp(in_name, rel_path, target_len) == 0 && 
                (in_name[target_len] == '/' || in_name[target_len] == '\\')) {
                sub_name = in_name + target_len + 1;
                match = 1;
            }
        }

        if (match && sub_name && sub_name[0] != '\0') {
            // Sadece ilk seviyedeki dosya/klasör adını alacağız (örn: "Program Files/calculator/calc.kef" -> "calculator" kısmı veya dosya adı)
            char direct_child[KRYFS_MAX_FILENAME];
            int j = 0;
            while (sub_name[j] != '\0' && sub_name[j] != '/' && sub_name[j] != '\\' && j < KRYFS_MAX_FILENAME - 1) {
                direct_child[j] = sub_name[j];
                j++;
            }
            direct_child[j] = '\0';

            // Bu ismi daha önce out_list'e ekledik mi kontrol et (Unique check)
            int already_added = 0;
            for (int k = 0; k < count; k++) {
                if (strcmp(out_list[k].name, direct_child) == 0) {
                    already_added = 1;
                    break;
                }
            }

            if (!already_added) {
                // İsmi kopyala
                int c = 0;
                while (direct_child[c] != '\0' && c < 255) {
                    out_list[count].name[c] = direct_child[c];
                    c++;
                }
                out_list[count].name[c] = '\0';

                // Eğer direkt çocuk ile tam in_name aynı ise bu bir dosyadır veya o satırdaki öğedir.
                // Eğer içinde hala '/' varsa veya başka bir inode bu alt klasöre aitse bu bir klasördür ([is_directory = true]).
                int is_dir = (strcmp(sub_name, direct_child) != 0) || inode->is_directory;
                out_list[count].is_directory = is_dir ? 1 : 0;
                out_list[count].size = is_dir ? 0 : inode->size;

                count++;
            }
        }
    }

    kfree(sector_buf);
    return count;
}

fs_driver_t kryfs_get_driver(void) {
    fs_driver_t driver;
    driver.read_file = kryfs_read_file;
    driver.list_dir = NULL;
    driver.get_dir_files = kryfs_get_dir_files;
    return driver;
}