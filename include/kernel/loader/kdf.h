#ifndef KERNEL_LOADER_KDF_H
#define KERNEL_LOADER_KDF_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* 'KDF1' Magic Number - K (0x4B), D (0x44), F (0x46), 1 (0x31) */
#define KDF_MAGIC 0x3146444B

/* Header structure for Kryon Driver Format (KDF v1) */
typedef struct {
    uint32_t magic;          /* KDF signature ('KDF1') */
    uint8_t  ver_major;      /* Major driver version */
    uint8_t  ver_minor;      /* Minor driver version */
    uint16_t flags;          /* Driver flags/capabilities */
    char     name[32];       /* Null-terminated driver name (e.g., "ac97_audio") */
    uint32_t text_offset;    /* File offset to the code section (.text) */
    uint32_t text_size;      /* Size of the code section in bytes */
    uint32_t init_entry;     /* Offset to the driver entry point (init_module) */
    uint32_t cleanup_entry;  /* Offset to the driver cleanup point (cleanup_module) */
} __attribute__((packed)) kdf_header_t;

/* Kernel API Jump Table passed to drivers upon initialization */
typedef struct {
    void* (*kmalloc)(size_t size);
    void  (*kfree)(void* ptr);
    void  (*serial_write)(const char* str);
    void  (*outb)(uint16_t port, uint8_t val);
    uint8_t (*inb)(uint16_t port);
    void  (*outw)(uint16_t port, uint16_t val);
    uint16_t (*inw)(uint16_t port);
    void  (*outl)(uint16_t port, uint32_t val);
    uint32_t (*inl)(uint16_t port);
} kdf_api_t;

/* Driver lifecycle function pointer types */
typedef int (*kdf_init_fn_t)(kdf_api_t* api);
typedef void (*kdf_cleanup_fn_t)(void);

/* Runtime driver module structure tracked by the kernel */
typedef struct {
    char name[32];
    void* base_address;
    uint32_t size;
    kdf_init_fn_t init;
    kdf_cleanup_fn_t cleanup;
    bool is_active;
} kdf_module_t;

/* Public KDF loader and management API */
int kdf_load_driver(const char* filepath);
int kdf_unload_driver(const char* driver_name);
void kdf_init_autoload(void);

#endif /* KERNEL_LOADER_KDF_H */