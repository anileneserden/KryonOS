#ifndef KRYFS_H
#define KRYFS_H

#include <stdint.h>

#define KRYFS_MAX_FILENAME 32

typedef struct {
    uint32_t magic;              // Dosya sistemi imzası (0x4B525946)
    uint32_t total_blocks;       // Toplam blok sayısı
    uint32_t inode_table_block;  // Inode tablosunun başlangıç bloğu
    uint32_t data_block_start;   // Veri bloklarının başlangıç bloğu
} __attribute__((packed)) kryfs_superblock_t;

typedef struct {
    uint8_t  is_used;                        // Bu inode dolu mu? (1 = Dolu, 0 = Boş)
    uint8_t  is_directory;                   // Dizin mi, dosya mı?
    uint32_t size;                           // Dosya boyutu (bayt cinsinden)
    uint32_t start_block;                    // Verinin balşadığı ilk blok indeksi
    char     filename[KRYFS_MAX_FILENAME];   // Dosya veya klasör adı
} __attribute__((packed)) kryfs_inode_t;

int kryfs_mount(void);
int kryfs_find_inode(const char* filename, kryfs_inode_t* out_inode);

#endif