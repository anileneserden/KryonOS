#include <ui/cursor.h>
#include <kernel/drivers/input/mouse_ps2.h>
#include <kernel/drivers/video/fb.h>

static int32_t old_mouse_x = 400;
static int32_t old_mouse_y = 300;
static uint32_t cursor_bg_buffer[CURSOR_WIDTH * CURSOR_HEIGHT];

// İmleci ekrana çizen yardımcı fonksiyon
static void draw_cursor_pixels(int32_t x, int32_t y) {
    for (int cy = 0; cy < CURSOR_HEIGHT; cy++) {
        for (int cx = 0; cx < CURSOR_WIDTH; cx++) {
            int px = x + cx;
            int py = y + cy;
            if (px >= 0 && (uint32_t)px < fb_get_width() && py >= 0 && (uint32_t)py < fb_get_height()) {
                fb_putpixel(px, py, 0xFFFFFFFF); // Beyaz kare imleç testi
            }
        }
    }
}

void cursor_init(void) {
    // Fare sürücüsünün başladığı güncel koordinatları al
    old_mouse_x = mouse_x;
    old_mouse_y = mouse_y;
    
    // 1. O anki konumun arka planını güvenli bir şekilde tampona kaydet (Artık ekran mavi/boyalı olacak)
    for (int y = 0; y < CURSOR_HEIGHT; y++) {
        for (int x = 0; x < CURSOR_WIDTH; x++) {
            int px = old_mouse_x + x;
            int py = old_mouse_y + y;
            if (px >= 0 && (uint32_t)px < fb_get_width() && py >= 0 && (uint32_t)py < fb_get_height()) {
                cursor_bg_buffer[y * CURSOR_WIDTH + x] = fb_getpixel(px, py);
            }
        }
    }

    // 2. Açılışta imlecin hemen görünmesi için ilk çizimi gerçekleştir
    draw_cursor_pixels(old_mouse_x, old_mouse_y);
}

void cursor_update(void) {
    if (mouse_x == old_mouse_x && mouse_y == old_mouse_y) {
        return; // Hareket yoksa işlem yapma
    }

    // 1. Adım: Önce eski konumdaki arka planı ekrana geri yükle (Eski imleci tamamen sil)
    for (int y = 0; y < CURSOR_HEIGHT; y++) {
        for (int x = 0; x < CURSOR_WIDTH; x++) {
            int px = old_mouse_x + x;
            int py = old_mouse_y + y;
            if (px >= 0 && (uint32_t)px < fb_get_width() && py >= 0 && (uint32_t)py < fb_get_height()) {
                fb_putpixel(px, py, cursor_bg_buffer[y * CURSOR_WIDTH + x]);
            }
        }
    }

    // 2. Adım: Yeni konumun arkasındaki pikselleri kaybetmemek için tampona kaydet
    for (int y = 0; y < CURSOR_HEIGHT; y++) {
        for (int x = 0; x < CURSOR_WIDTH; x++) {
            int px = mouse_x + x;
            int py = mouse_y + y;
            if (px >= 0 && (uint32_t)px < fb_get_width() && py >= 0 && (uint32_t)py < fb_get_height()) {
                cursor_bg_buffer[y * CURSOR_WIDTH + x] = fb_getpixel(px, py);
            }
        }
    }

    // 3. Adım: Yeni konuma imleci çiz
    draw_cursor_pixels(mouse_x, mouse_y);

    // 4. Adım: Eski koordinatları güncelle
    old_mouse_x = mouse_x;
    old_mouse_y = mouse_y;
}