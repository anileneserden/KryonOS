#ifndef KERNEL_LEX_H
#define KERNEL_LEX_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <kernel/fs/vfs.h>

#define LEX_MAGIC 0x0058454C
#define LEX_VERSION 1
#define LEX_ARCH_I386 1
#define LEX_MAX_SIZE 16384
#define LEX_LOAD_ADDRESS 0x400000
#define LEX_API_ADDRESS 0x501000

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
} __attribute__((packed)) lex_header_t;

typedef int (*lex_entry_t)(void);

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
    int (*input_get_text)(int input_id, char* out_buf, int max_len);
    
    int (*combobox_create)(int x, int y, int w, int h, const char** items, int item_count, int default_index, uint32_t bg_color, uint32_t text_color, uint32_t border_color, uint8_t anchor);

    int (*get_directory_files)(const char* full_path, vfs_file_info_t* out_list, int max_count);
    void* (*read_file)(const char* full_path, uint32_t* out_size);

    int (*strcmp)(const char* s1, const char* s2);
    size_t (*strlen)(const char* str);
    void (*yield)(void);

    uint32_t* (*canvas_create)(int x, int y, int w, int h, uint8_t anchor, int* out_canvas_id);
    void (*canvas_update_buffer)(int canvas_id);
    
    void (*background_color)(uint32_t color);

    void (*reboot_system)(void);
    void (*shutdown_system)(void);

    bool (*is_key_pressed)(int key_code);
} lex_api_t;

bool lex_load_and_run(const char* path);

#endif