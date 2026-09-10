#include <kernel/drivers/video/fb.h>
#include <kernel/serial.h>
#include <kernel/mem/heap.h>

static volatile uint32_t* fb_address = 0;
static uint32_t* back_buffer = 0;
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

    // Çözünürlüğe uygun back-buffer boyutunu heap üzerinden dinamik olarak tahsis et
    uint32_t total_bytes = fb_height * fb_pitch;
    back_buffer = (uint32_t*)kmalloc(total_bytes);

    if (back_buffer) {
        serial_write("Back-buffer heap uzerinden basariyla olusturuldu.\n");
    } else {
        serial_write("HATA (fb): Back-buffer icin yeterli heap alani bulunamadi!\n");
    }

    serial_write("Framebuffer basariyla baslatildi.\n");
}

void fb_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!back_buffer || x >= fb_width || y >= fb_height) {
        return;
    }
    uint32_t pps = fb_pitch / 4;
    back_buffer[y * pps + x] = color;
}

uint32_t fb_getpixel(uint32_t x, uint32_t y) {
    if (!back_buffer || x >= fb_width || y >= fb_height) {
        return 0;
    }
    
    uint32_t pps = fb_pitch / 4;
    return back_buffer[y * pps + x];
}

void fb_clear(uint32_t color) {
    if (!back_buffer) return;

    uint32_t pps = fb_pitch / 4;
    for (uint32_t y = 0; y < fb_height; y++) {
        for (uint32_t x = 0; x < fb_width; x++) {
            back_buffer[y * pps + x] = color;
        }
    }
}

void fb_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    if (!back_buffer) return;

    if (x >= fb_width || y >= fb_height) return;
    if (x + width > fb_width) width = fb_width - x;
    if (y + height > fb_height) height = fb_height - y;

    uint32_t pps = fb_pitch / 4;
    for (uint32_t ry = 0; ry < height; ry++) {
        for (uint32_t rx = 0; rx < width; rx++) {
            back_buffer[(y + ry) * pps + (x + rx)] = color;
        }
    }
}

void fb_swap(void) {
    if (!fb_address || !back_buffer) return;

    volatile uint32_t* dst = (volatile uint32_t*)fb_address;
    uint32_t* src = (uint32_t*)back_buffer;
    uint32_t pps = fb_pitch / 4;

    for (uint32_t y = 0; y < fb_height; y++) {
        for (uint32_t x = 0; x < fb_width; x++) {
            dst[y * pps + x] = src[y * pps + x];
        }
    }
}

// Sadece belirtilen dikdörtgen alanı back_buffer'dan gerçek VRAM'e kopyalayan fonksiyon
void fb_blit_region(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (!fb_address || !back_buffer) return;
    if (x >= fb_width || y >= fb_height) return;
    if (x + width > fb_width) width = fb_width - x;
    if (y + height > fb_height) height = fb_height - y;

    volatile uint32_t* dst = (volatile uint32_t*)fb_address;
    uint32_t* src = (uint32_t*)back_buffer;
    uint32_t pps = fb_pitch / 4;

    for (uint32_t ry = 0; ry < height; ry++) {
        for (uint32_t rx = 0; rx < width; rx++) {
            uint32_t idx = (y + ry) * pps + (x + rx);
            dst[idx] = src[idx];
        }
    }
}

uint32_t fb_get_width(void) {
    return fb_width;
}

uint32_t fb_get_height(void) {
    return fb_height;
}

volatile uint32_t* fb_get_address(void) {
    return fb_address;
}

uint32_t fb_get_pitch(void) {
    return fb_pitch;
}