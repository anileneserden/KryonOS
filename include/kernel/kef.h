#ifndef KERNEL_KEF_H
#define KERNEL_KEF_H

#include <stdint.h>
#include <stdbool.h>

#define KEF_MAGIC 0x0046454B /* "KEF\0" */
#define KEF_VERSION 1
#define KEF_ARCH_I386 1
#define KEF_MAX_SIZE 16384
#define KEF_LOAD_ADDRESS 0x400000
#define KEF_API_ADDRESS 0x501000

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
    int (*label_create)(int x, int y, uint32_t color, const char* text);
    int (*panel_create)(int x, int y, int w, int h, uint32_t color);
    int (*button_create)(int x, int y, int w, int h, uint32_t bg_color, uint32_t text_color, const char* text);
} kef_api_t;

bool kef_load_and_run(const char* path);

#endif