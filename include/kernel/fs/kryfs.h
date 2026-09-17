#ifndef KERNEL_FS_KRYFS_H
#define KERNEL_FS_KRYFS_H

#include <stdint.h>

#define KRYFS_MAGIC 0x4B525946
#define KRYFS_BLOCK_SIZE 512
#define KRYFS_MAX_INODES 64

typedef struct {
    uint32_t magic;
    uint32_t total_sectors;
    uint32_t inode_count;
    uint32_t block_size;
    char volume_name[32];
} __attribute__((packed)) kryfs_superblock_t;

typedef struct {
    uint32_t inode_id;     // 4 bytes
    char filename[32];     // 32 bytes
    uint32_t size;         // 4 bytes
    uint32_t first_block;  // 4 bytes
    uint8_t is_used;       // 1 byte
    uint8_t is_directory;  // 1 byte
} __attribute__((packed)) kryfs_inode_t;

void kryfs_init(void);
void kryfs_format(void);
void* kryfs_read_file(const char* filename, uint32_t* out_size);
void kryfs_list_files(void);
void kryos_fs_system_init(void);
int kryfs_get_files(kryfs_inode_t* out_inodes, int max_count);

#endif