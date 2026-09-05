#include <kernel/drivers/video/fb.h>
#include <kernel/serial.h>

static volatile uint32_t* fb_address = 0;
static uint32_t fb_width = 0;
static uint32_t fb_height = 0;
static uint32_t fb_pitch = 0;

void fb_init(multiboot_info_t* mboot) {
    if (!(mboot->flags & (1 << 12))) {
        serial_write("HATA (fb): Framebuffer bilgisi bulunamadi!\n");
        return;
    }

    fb_address = (volatile uint32_t*)(uintptr_t)mboot->framebuffer_addr;
    fb_width = mboot->framebuffer_width;
    fb_height = mboot->framebuffer_height;
    fb_pitch = mboot->framebuffer_pitch;

    serial_write("Framebuffer basariyla baslatildi.\n");
}

void fb_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!fb_address || x >= fb_width || y >= fb_height) {
        return;
    }
    uint32_t pps = fb_pitch / 4;
    fb_address[y * pps + x] = color;
}

uint32_t fb_getpixel(uint32_t x, uint32_t y) {
    if (!fb_address || x >= fb_width || y >= fb_height) {
        return 0; // Sınırlar dışındaysa veya adres yoksa siyah/boş dön
    }
    
    uint32_t pps = fb_pitch / 4;
    return fb_address[y * pps + x];
}

void fb_clear(uint32_t color) {
    if (!fb_address) return;

    uint32_t pps = fb_pitch / 4;
    for (uint32_t y = 0; y < fb_height; y++) {
        for (uint32_t x = 0; x < fb_width; x++) {
            fb_address[y * pps + x] = color;
        }
    }
}

void fb_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    if (!fb_address) return;

    // Ekran sınırları dışına taşmaları engelle
    if (x >= fb_width || y >= fb_height) return;
    if (x + width > fb_width) width = fb_width - x;
    if (y + height > fb_height) height = fb_height - y;

    uint32_t pps = fb_pitch / 4;
    for (uint32_t ry = 0; ry < height; ry++) {
        for (uint32_t rx = 0; rx < width; rx++) {
            fb_address[(y + ry) * pps + (x + rx)] = color;
        }
    }
}

uint32_t fb_get_width(void) {
    return fb_width;
}

uint32_t fb_get_height(void) {
    return fb_height;
}