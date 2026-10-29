#ifndef LNRFS_H
#define LNRFS_H

#include <stdint.h>
#include <kernel/fs/vfs.h>

#define LNRFS_MAX_FILENAME 256

typedef struct {
    uint32_t magic;              
    uint32_t total_blocks;       
    uint32_t inode_table_block;  
    uint32_t data_block_start;   
} __attribute__((packed)) lnrfs_superblock_t;

typedef struct {
    uint8_t  is_used;                        
    uint8_t  is_directory;                   
    uint32_t size;                           
    uint32_t start_block;                    
    char     filename[LNRFS_MAX_FILENAME];   
} __attribute__((packed)) lnrfs_inode_t;

int lnrfs_mount(void);
int lnrfs_find_inode(const char* filename, lnrfs_inode_t* out_inode);
void* lnrfs_read_file(const char* filename, uint32_t* out_size);
int lnrfs_get_dir_files(const char* rel_path, vfs_file_info_t* out_list, int max_count);
fs_driver_t lnrfs_get_driver(void);

#endif