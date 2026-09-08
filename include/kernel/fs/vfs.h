#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stdbool.h>

// Sürücü (Filesystem) operasyon arayüzü
typedef struct {
    void* (*read_file)(const char* path, uint32_t* out_size);
    void (*list_dir)(void);
} fs_driver_t;

// Sürücü bağlama noktası (Mount Entry)
typedef struct {
    char drive_letter;     // Örn: 'C'
    char volume_name[32];  // Örn: "KryonVolume"
    fs_driver_t driver;    // Dosya sistemi sürücü fonksiyonları
    bool is_mounted;
} vfs_mount_t;

void vfs_init(void);
bool vfs_mount(char drive_letter, const char* volume_name, fs_driver_t driver);
void* vfs_read_file(const char* full_path, uint32_t* out_size);
void vfs_list_drive(char drive_letter);

#endif