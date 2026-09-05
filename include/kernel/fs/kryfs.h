#ifndef KERNEL_FS_KRYFS_H
#define KERNEL_FS_KRYFS_H

#include <stdint.h>

#define KRYFS_MAGIC 0x4B525953 // "KRYS"
#define KRYFS_BLOCK_SIZE 512

typedef struct {
    uint32_t magic;
    uint32_t total_sectors;
    uint32_t inode_count;
    uint32_t block_size;
    char volume_name[32];
} __attribute__((packed)) kryfs_superblock_t;

typedef struct {
    uint32_t inode_id;
    char filename[32];
    uint32_t size;
    uint32_t first_block;
    uint8_t is_used;
} __attribute__((packed)) kryfs_inode_t;

void kryfs_init(void);
void kryfs_format(void);
void* kryfs_read_file(const char* filename, uint32_t* out_size);

#endif