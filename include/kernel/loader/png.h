#ifndef KERNEL_LOADER_PNG_H
#define KERNEL_LOADER_PNG_H

#include <stdint.h>

typedef struct {
    int width;
    int height;
    int channels;
    uint32_t* pixels; // RGBA formatında piksel verisi
} png_image_t;

png_image_t* png_load_from_file(const char* filepath);
void png_free_image(png_image_t* img);

#endif