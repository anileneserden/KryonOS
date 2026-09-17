#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>
#include <stdbool.h>
#include <kernel/fs/vfs.h>

// FAT32 BIOS Parameter Block (BPB) and Extended BPB structure
typedef struct {
    uint8_t  boot_jump[3];
    uint8_t  oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t  num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t table_size_16;
    uint16_t sectors_per_track;
    uint16_t head_side_count;
    uint32_t hidden_sector_count;
    uint32_t total_sectors_32;

    // FAT32 Extended Fields
    uint32_t table_size_32;
    uint16_t extended_flags;
    uint16_t fat_version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    uint8_t  volume_label[11];
    uint8_t  file_system_type[8];
} __attribute__((packed)) fat32_bpb_t;

// Standard 32-byte FAT directory entry structure
typedef struct {
    uint8_t  name[11];       // 8-byte filename, 3-byte extension (8.3 format)
    uint8_t  attr;           // File attributes (0x10 = directory, 0x20 = archive/file)
    uint8_t  nt_reserved;
    uint8_t  creation_time_tenths;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high; // High 16 bits of the first cluster
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_low;  // Low 16 bits of the first cluster
    uint32_t file_size;          // File size in bytes
} __attribute__((packed)) fat32_dir_entry_t;

// Function prototypes
fs_driver_t fat32_get_driver(void);
bool fat32_init_disk(uint8_t drive, uint32_t lba_start);

#endif