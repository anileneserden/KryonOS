#ifndef KERNEL_KEF2_H
#define KERNEL_KEF2_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define KEF2_MAGIC 0x3246454B
#define KEF2_VERSION 2
#define KEF2_ARCH_I386 1
#define KEF2_LOAD_ADDRESS ((uint32_t)0x400000)
#define KEF_API_ADDRESS   ((uint32_t)0x501000)

#define KEF2_SECTION_TEXT      1
#define KEF2_SECTION_RESOURCES 2

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t architecture;
    uint32_t section_count;
    uint32_t flags;
} __attribute__((packed)) kef2_header_t;

typedef struct {
    uint32_t type;
    uint32_t offset;
    uint32_t size;
} __attribute__((packed)) kef2_section_header_t;

typedef struct {
    char name[64];
    uint32_t offset;
    uint32_t size;
} __attribute__((packed)) kef2_resource_entry_t;

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
    int (*get_directory_files)(const char* full_path, void* out_list, int max_count);
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
} kef_api_t;

typedef int (*kef_entry_t)(void);

bool kef2_load_and_run(const char* path);

#endif