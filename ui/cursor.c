#include <ui/cursor.h>
#include <kernel/drivers/input/mouse_ps2.h>
#include <kernel/drivers/video/fb.h>

static int32_t old_mouse_x = 400;
static int32_t old_mouse_y = 300;
static uint32_t cursor_bg_buffer[CURSOR_WIDTH * CURSOR_HEIGHT];

// fb.c içerisindeki bölgesel blit fonksiyonunun prototipi
extern void fb_blit_region(uint32_t x, uint32_t y, uint32_t width, uint32_t height);

void cursor_init(void) {
    old_mouse_x = mouse_x;
    old_mouse_y = mouse_y;
    
    // İlk konumun arka planını kaydet
    for (int y = 0; y < CURSOR_HEIGHT; y++) {
        for (int x = 0; x < CURSOR_WIDTH; x++) {
            int px = old_mouse_x + x;
            int py = old_mouse_y + y;
            if (px >= 0 && (uint32_t)px < fb_get_width() && py >= 0 && (uint32_t)py < fb_get_height()) {
                cursor_bg_buffer[y * CURSOR_WIDTH + x] = fb_getpixel(px, py);
            }
        }
    }
}

// Sadece fare hareket ettiğinde çağrılan optimize fonksiyon
void cursor_update_and_redraw(void) {
    if (mouse_x == old_mouse_x && mouse_y == old_mouse_y) {
        return; // Fare oynamadıysa hiçbir şey yapma (CPU'yu yorma)
    }

    // 1. ADIM: Eski konumdaki arka planı back_buffer'a geri yükle (Eski imleci sil)
    for (int y = 0; y < CURSOR_HEIGHT; y++) {
        for (int x = 0; x < CURSOR_WIDTH; x++) {
            int px = old_mouse_x + x;
            int py = old_mouse_y + y;
            if (px >= 0 && (uint32_t)px < fb_get_width() && py >= 0 && (uint32_t)py < fb_get_height()) {
                fb_putpixel(px, py, cursor_bg_buffer[y * CURSOR_WIDTH + x]);
            }
        }
    }
    // Sadece eski imleç karesini ekrana yansıt
    fb_blit_region(old_mouse_x, old_mouse_y, CURSOR_WIDTH, CURSOR_HEIGHT);

    // 2. ADIM: Yeni konumun arka planını kaydet
    for (int y = 0; y < CURSOR_HEIGHT; y++) {
        for (int x = 0; x < CURSOR_WIDTH; x++) {
            int px = mouse_x + x;
            int py = mouse_y + y;
            if (px >= 0 && (uint32_t)px < fb_get_width() && py >= 0 && (uint32_t)py < fb_get_height()) {
                cursor_bg_buffer[y * CURSOR_WIDTH + x] = fb_getpixel(px, py);
            }
        }
    }

    // 3. ADIM: Yeni konuma imleci çiz
    for (int y = 0; y < CURSOR_HEIGHT; y++) {
        for (int x = 0; x < CURSOR_WIDTH; x++) {
            int px = mouse_x + x;
            int py = mouse_y + y;
            if (px >= 0 && (uint32_t)px < fb_get_width() && py >= 0 && (uint32_t)py < fb_get_height()) {
                fb_putpixel(px, py, 0xFFFFFFFF); // Beyaz imleç
            }
        }
    }
    // Sadece yeni imleç karesini ekrana yansıt
    fb_blit_region(mouse_x, mouse_y, CURSOR_WIDTH, CURSOR_HEIGHT);

    // Konumları güncelle
    old_mouse_x = mouse_x;
    old_mouse_y = mouse_y;
}