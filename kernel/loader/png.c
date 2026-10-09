#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#define STBI_ONLY_PNG

// Missing type definitions and macro workarounds for stb_image.h
typedef struct FILE FILE;
#define assert(x) ((void)0)

#define STB_IMAGE_IMPLEMENTATION
#include <kernel/external/stb_image.h>

#include <kernel/loader/png.h>
#include <kernel/fs/vfs.h>
#include <kernel/serial.h>
#include <stdlib.h>

// Macros for RGBA32 pixel components (0xAABBGGRR / 0xRRGGBBAA layout)
#define GET_R(p) ((p) & 0xFF)
#define GET_G(p) (((p) >> 8) & 0xFF)
#define GET_B(p) (((p) >> 16) & 0xFF)
#define GET_A(p) (((p) >> 24) & 0xFF)
#define MAKE_RGBA(r, g, b, a) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(a) << 24))

png_image_t* png_load_from_file(const char* filepath) {
    uint32_t file_size = 0;
    void* file_data = vfs_read_file(filepath, &file_size);

    if (!file_data || file_size == 0) {
        serial_write("[PNG ERROR] Failed to read file via VFS: ");
        serial_write((char*)filepath);
        serial_write("\n");
        return NULL;
    }

    int width, height, channels;
    
    // Force 4 channels (RGBA) so the output is ready for direct display
    unsigned char* img_data = stbi_load_from_memory(
        (unsigned char*)file_data, 
        file_size, 
        &width, 
        &height, 
        &channels, 
        4
    );

    // Free raw VFS file buffer now that decoding is complete
    kfree(file_data);

    if (!img_data) {
        serial_write("[PNG ERROR] stb_image failed to decode the image!\n");
        return NULL;
    }

    png_image_t* image = (png_image_t*)malloc(sizeof(png_image_t));
    if (!image) {
        stbi_image_free(img_data);
        return NULL;
    }

    image->width = width;
    image->height = height;
    image->channels = 4;
    image->pixels = (uint32_t*)img_data;

    serial_write("[PNG] Image successfully loaded and decoded!\n");
    return image;
}

png_image_t* png_resize(const png_image_t* src, int target_w, int target_h) {
    if (!src || !src->pixels || target_w <= 0 || target_h <= 0) {
        serial_write("[PNG ERROR] Invalid resize parameters!\n");
        return NULL;
    }

    png_image_t* dst = (png_image_t*)malloc(sizeof(png_image_t));
    if (!dst) return NULL;

    dst->width = target_w;
    dst->height = target_h;
    dst->channels = 4;
    dst->pixels = (uint32_t*)malloc(target_w * target_h * sizeof(uint32_t));

    if (!dst->pixels) {
        free(dst);
        return NULL;
    }

    // Fixed-point 16.16 scaling ratios
    uint32_t x_ratio = ((uint32_t)src->width << 16) / (uint32_t)target_w;
    uint32_t y_ratio = ((uint32_t)src->height << 16) / (uint32_t)target_h;

    for (int y = 0; y < target_h; y++) {
        for (int x = 0; x < target_w; x++) {
            
            int src_x_start = (int)((x * x_ratio) >> 16);
            int src_x_end   = (int)(((x + 1) * x_ratio) >> 16);
            int src_y_start = (int)((y * y_ratio) >> 16);
            int src_y_end   = (int)(((y + 1) * y_ratio) >> 16);

            if (src_x_end >= src->width)   src_x_end = src->width - 1;
            if (src_y_end >= src->height)  src_y_end = src->height - 1;

            uint32_t r_sum = 0, g_sum = 0, b_sum = 0, a_sum = 0;
            uint32_t count = 0;

            for (int sy = src_y_start; sy <= src_y_end; sy++) {
                for (int sx = src_x_start; sx <= src_x_end; sx++) {
                    uint32_t p = src->pixels[sy * src->width + sx];
                    r_sum += GET_R(p);
                    g_sum += GET_G(p);
                    b_sum += GET_B(p);
                    a_sum += GET_A(p);
                    count++;
                }
            }

            if (count > 0) {
                uint8_t r = (uint8_t)(r_sum / count);
                uint8_t g = (uint8_t)(g_sum / count);
                uint8_t b = (uint8_t)(b_sum / count);
                uint8_t a = (uint8_t)(a_sum / count);
                dst->pixels[y * target_w + x] = MAKE_RGBA(r, g, b, a);
            }
        }
    }

    serial_write("[PNG] Image successfully resized!\n");
    return dst;
}

void png_free_image(png_image_t* img) {
    if (img) {
        if (img->pixels) {
            stbi_image_free(img->pixels);
        }
        free(img);
    }
}