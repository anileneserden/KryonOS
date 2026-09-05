#include <ui/cursor.h>
#include <kernel/drivers/input/mouse_ps2.h>
#include <kernel/drivers/video/fb.h>

static int32_t old_mouse_x = 400;
static int32_t old_mouse_y = 300;
static uint32_t cursor_bg_buffer[CURSOR_WIDTH * CURSOR_HEIGHT];

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

void cursor_update(void) {
    if (mouse_x == old_mouse_x && mouse_y == old_mouse_y) {
        return; // Hareket yoksa işlem yapma
    }

    // 1. Eski konuma arka planı geri yükle (İmleci sil)
    for (int y = 0; y < CURSOR_HEIGHT; y++) {
        for (int x = 0; x < CURSOR_WIDTH; x++) {
            int px = old_mouse_x + x;
            int py = old_mouse_y + y;
            if (px >= 0 && (uint32_t)px < fb_get_width() && py >= 0 && (uint32_t)py < fb_get_height()) {
                fb_putpixel(px, py, cursor_bg_buffer[y * CURSOR_WIDTH + x]);
            }
        }
    }

    // 2. Yeni konumun arka planını tampona kaydet
    for (int y = 0; y < CURSOR_HEIGHT; y++) {
        for (int x = 0; x < CURSOR_WIDTH; x++) {
            int px = mouse_x + x;
            int py = mouse_y + y;
            if (px >= 0 && (uint32_t)px < fb_get_width() && py >= 0 && (uint32_t)py < fb_get_height()) {
                cursor_bg_buffer[y * CURSOR_WIDTH + x] = fb_getpixel(px, py);
            }
        }
    }

    // 3. Yeni konuma imleci çiz (Şimdilik test için beyaz kutu, ileride KBI formatı bağlanacak)
    for (int y = 0; y < CURSOR_HEIGHT; y++) {
        for (int x = 0; x < CURSOR_WIDTH; x++) {
            int px = mouse_x + x;
            int py = mouse_y + y;
            if (px >= 0 && (uint32_t)px < fb_get_width() && py >= 0 && (uint32_t)py < fb_get_height()) {
                fb_putpixel(px, py, 0xFFFFFFFF);
            }
        }
    }

    // 4. Eski koordinatları güncelle
    old_mouse_x = mouse_x;
    old_mouse_y = mouse_y;
}