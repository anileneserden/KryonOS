#ifndef KERNEL_KEF_H
#define KERNEL_KEF_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <kernel/fs/vfs.h>

#define KEF_MAGIC 0x0046454B
#define KEF_VERSION 1
#define KEF_ARCH_I386 1
#define KEF_MAX_SIZE 16384
#define KEF_LOAD_ADDRESS 0x400000
#define KEF_API_ADDRESS 0x501000

#define ANCHOR_NONE     0
#define ANCHOR_LEFT     (1 << 0)
#define ANCHOR_RIGHT    (1 << 1)
#define ANCHOR_TOP      (1 << 2)
#define ANCHOR_BOTTOM   (1 << 3)

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t architecture;
    uint32_t entry_offset;
    uint32_t payload_size;
    uint32_t flags;
    uint32_t header_size;
} __attribute__((packed)) kef_header_t;

typedef int (*kef_entry_t)(void);

typedef struct {
    int (*window_create)(const char* title, int width, int height);
    void (*print)(const char* str);
    void (*exit)(void);
    int (*label_create)(int x, int y, uint32_t color, const char* text, uint8_t anchor);
    int (*panel_create)(int x, int y, int w, int h, uint32_t color, uint32_t hover_color, void (*on_click)(void), void (*on_hover)(void), uint8_t anchor);
    int (*button_create)(int x, int y, int w, int h, uint32_t bg_color, uint32_t text_color, const char* text, void (*on_click)(void), uint8_t anchor);
    int (*input_create)(int x, int y, int w, int h, 
                        const char* text, const char* placeholdertext, 
                        uint32_t backcolor, uint32_t color, 
                        uint32_t placeholder_color, uint32_t border_color, 
                        int border_thickness, uint8_t anchor);
    int (*get_directory_files)(const char* full_path, vfs_file_info_t* out_list, int max_count);
    void* (*read_file)(const char* full_path, uint32_t* out_size);

    int (*strcmp)(const char* s1, const char* s2);
    size_t (*strlen)(const char* str);
    void (*yield)(void);
} kef_api_t;

bool kef_load_and_run(const char* path);

#endif