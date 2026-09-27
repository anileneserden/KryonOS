#include <kernel/drivers/video/fb.h>
#include <kernel/multiboot.h>
#include <kernel/mem/vmm.h>
#include <kernel/mem/heap.h>
#include <kernel/serial.h>
#include <kernel/string.h>

static uint32_t* framebuffer_addr = 0;
static uint32_t* back_buffer = 0;
static uint32_t fb_width = 0;
static uint32_t fb_height = 0;
static uint32_t fb_pitch = 0;
static uint32_t fb_bpp = 0;

void fb_init(multiboot_info_t* mboot) {
    if (!mboot) return;

    framebuffer_addr = (uint32_t*)(uintptr_t)mboot->framebuffer_addr;
    fb_width = mboot->framebuffer_width;
    fb_height = mboot->framebuffer_height;
    fb_pitch = mboot->framebuffer_pitch;
    fb_bpp = mboot->framebuffer_bpp;

    if (!framebuffer_addr || fb_width == 0 || fb_height == 0) {
        serial_write("FB: Invalid framebuffer parameters!\n");
        return;
    }

    uint32_t fb_size = fb_height * fb_pitch;

    // GERÇEK DONANIM DÜZELTMESİ:
    // vmm_map_page döngüsü kaldırıldı. Bootloader bu alanı zaten haritaladı.
    // kmalloc patlamasını önlemek için güvenli alokasyon
    back_buffer = (uint32_t*)kmalloc(fb_size);
    if (!back_buffer) {
        serial_write("FB: Back buffer allocation failed, falling back to direct VRAM!\n");
        // Back buffer aloke edilemezse sistemi kilitlemek yerine doğrudan VRAM'e yazacak şekilde ayarla
        back_buffer = framebuffer_addr;
        return;
    }

    memset(back_buffer, 0, fb_size);
    serial_write("FB: Framebuffer initialized successfully.\n");
}

void fb_clear(uint32_t color) {
    if (!back_buffer) return;
    uint32_t total_pixels = (fb_pitch / 4) * fb_height;
    for (uint32_t i = 0; i < total_pixels; i++) {
        back_buffer[i] = color;
    }
}

void fb_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= fb_width || y >= fb_height || !back_buffer) return;
    uint32_t index = y * (fb_pitch / 4) + x;
    back_buffer[index] = color;
}

uint32_t fb_getpixel(uint32_t x, uint32_t y) {
    if (x >= fb_width || y >= fb_height || !back_buffer) return 0;
    uint32_t index = y * (fb_pitch / 4) + x;
    return back_buffer[index];
}

void fb_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t i = 0; i < h; i++) {
        for (uint32_t j = 0; j < w; j++) {
            fb_putpixel(x + j, y + i, color);
        }
    }
}

void fb_swap(void) {
    if (!framebuffer_addr || !back_buffer) return;
    // Eğer back_buffer doğrudan VRAM'i gösteriyorsa swap yapmaya gerek yok
    if (back_buffer == framebuffer_addr) return;

    uint32_t fb_size = fb_height * fb_pitch;
    memcpy(framebuffer_addr, back_buffer, fb_size);
}

void fb_blit_region(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (!framebuffer_addr || !back_buffer) return;
    if (back_buffer == framebuffer_addr) return;

    uint32_t pitch_pixels = fb_pitch / 4;

    for (uint32_t row = 0; row < height; row++) {
        uint32_t dest_y = y + row;
        if (dest_y >= fb_height) break;

        uint32_t offset = dest_y * pitch_pixels + x;
        uint32_t copy_width = width;
        if (x + copy_width > fb_width) {
            copy_width = fb_width - x;
        }

        memcpy(&framebuffer_addr[offset], &back_buffer[offset], copy_width * sizeof(uint32_t));
    }
}

uint32_t fb_get_width(void) { return fb_width; }
uint32_t fb_get_height(void) { return fb_height; }
uint32_t* fb_get_back_buffer(void) { return back_buffer; }
volatile uint32_t* fb_get_address(void) { return (volatile uint32_t*)framebuffer_addr; }
uint32_t fb_get_pitch(void) { return fb_pitch; }