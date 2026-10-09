#ifndef KERNEL_LOADER_PNG_H
#define KERNEL_LOADER_PNG_H

#include <stdint.h>

typedef struct {
    int width;
    int height;
    int channels;
    uint32_t* pixels; // 32-bit pixel data in RGBA format
} png_image_t;

png_image_t* png_load_from_file(const char* filepath);
png_image_t* png_resize(const png_image_t* src, int target_w, int target_h);
void png_free_image(png_image_t* img);

#endif