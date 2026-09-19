#ifndef KRYFS_H
#define KRYFS_H

#include <stdint.h>
#include <kernel/fs/vfs.h>

#define KRYFS_MAX_FILENAME 32

typedef struct {
    uint32_t magic;              
    uint32_t total_blocks;       
    uint32_t inode_table_block;  
    uint32_t data_block_start;   
} __attribute__((packed)) kryfs_superblock_t;

typedef struct {
    uint8_t  is_used;                        
    uint8_t  is_directory;                   
    uint32_t size;                           
    uint32_t start_block;                    
    char     filename[KRYFS_MAX_FILENAME];   
} __attribute__((packed)) kryfs_inode_t;

int kryfs_mount(void);
int kryfs_find_inode(const char* filename, kryfs_inode_t* out_inode);
void* kryfs_read_file(const char* filename, uint32_t* out_size);
fs_driver_t kryfs_get_driver(void);

#endif