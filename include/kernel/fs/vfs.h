#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stdbool.h>

// Common structure for file/directory information
typedef struct {
    char name[32];
    uint32_t size;
    bool is_directory;
} vfs_file_info_t;

// Filesystem driver operation interface
typedef struct {
    void* (*read_file)(const char* path, uint32_t* out_size);
    void (*list_dir)(void);
    int (*get_dir_files)(const char* path, vfs_file_info_t* out_list, int max_count);
    int (*create_file)(const char* path);
    int (*write_file)(const char* path, const void* buffer, uint32_t size);
    int (*mkdir)(const char* path);
    bool (*file_exists)(const char* path);
} fs_driver_t;

// Filesystem mount entry
typedef struct {
    char drive_letter;     // For example: 'C'
    char volume_name[32];  // For example: "KryonVolume"
    fs_driver_t driver;    // Filesystem driver functions
    bool is_mounted;
} vfs_mount_t;

void vfs_init(void);
bool vfs_mount(char drive_letter, const char* volume_name, fs_driver_t driver);
void* vfs_read_file(const char* full_path, uint32_t* out_size);
int vfs_create_file(const char* full_path);
int vfs_write_file(const char* full_path, const void* buffer, uint32_t size);
int vfs_mkdir(const char* full_path);
void vfs_list_drive(char drive_letter);
int vfs_get_directory_files(const char* full_path, vfs_file_info_t* out_list, int max_count);
bool vfs_file_exists(const char* full_path);

#endif