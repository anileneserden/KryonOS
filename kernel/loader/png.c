#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#define STBI_ONLY_PNG

// stb_image.h için eksik tip ve makro çözümleri
typedef struct FILE FILE;
#define assert(x) ((void)0)

#define STB_IMAGE_IMPLEMENTATION
#include <kernel/external/stb_image.h>

#include <kernel/loader/png.h>
#include <kernel/fs/vfs.h>
#include <kernel/serial.h>
#include <stdlib.h>

png_image_t* png_load_from_file(const char* filepath) {
    uint32_t file_size = 0;
    void* file_data = vfs_read_file(filepath, &file_size);

    if (!file_data || file_size == 0) {
        serial_write("[PNG ERROR] Dosya VFS üzerinden okunamadi: ");
        serial_write((char*)filepath);
        serial_write("\n");
        return NULL;
    }

    int width, height, channels;
    
    // stb_image, arka planda bizim yazdığımız malloc/free fonksiyonlarını kullanacak.
    // 4 kanal (RGBA) zorlayarak ekran framebuffer yapısına doğrudan uyumlu hale getiriyoruz.
    unsigned char* img_data = stbi_load_from_memory(
        (unsigned char*)file_data, 
        file_size, 
        &width, 
        &height, 
        &channels, 
        4
    );

    // Ham dosya verisi artık decode edildiği için VFS belleğini serbest bırakıyoruz
    kfree(file_data);

    if (!img_data) {
        serial_write("[PNG ERROR] stb_image resmi decode edemedi!\n");
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
    image->pixels = (uint32_t*)img_data; // stb_image çıktısı direkt 32-bit RGBA piksellerdir

    serial_write("[PNG] Resim basariyla yuklendi ve decode edildi!\n");
    return image;
}

void png_free_image(png_image_t* img) {
    if (img) {
        if (img->pixels) {
            stbi_image_free(img->pixels);
        }
        free(img);
    }
}