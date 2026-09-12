#include <ui/cursor.h>
#include <kernel/drivers/input/mouse_ps2.h>
#include <kernel/drivers/video/fb.h>
#include <stdbool.h>

static int32_t old_mouse_x = 400;
static int32_t old_mouse_y = 300;
static uint32_t cursor_bg_buffer[CURSOR_WIDTH * CURSOR_HEIGHT];
static bool cursor_visible = true;

extern void fb_blit_region(uint32_t x, uint32_t y, uint32_t width, uint32_t height);

void cursor_init(void) {
    // Fare sürücüsünün o anki gerçek konumunu doğrudan baz al
    old_mouse_x = mouse_x;
    old_mouse_y = mouse_y;
    cursor_visible = true;
    
    // Açılışta imlecin altındaki arka planı güvenli bir şekilde kaydet
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

void cursor_get_position(int32_t* x, int32_t* y) {
    if (x) *x = old_mouse_x;
    if (y) *y = old_mouse_y;
}

// Ekran/pencere çizilmeden önce imleci back-buffer'dan kaldırır.
void cursor_prepare_redraw(void) {
    if (!cursor_visible) return;

    for (int y = 0; y < CURSOR_HEIGHT; y++) {
        for (int x = 0; x < CURSOR_WIDTH; x++) {
            int px = old_mouse_x + x;
            int py = old_mouse_y + y;
            if (px >= 0 && (uint32_t)px < fb_get_width() && py >= 0 && (uint32_t)py < fb_get_height()) {
                fb_putpixel(px, py, cursor_bg_buffer[y * CURSOR_WIDTH + x]);
            }
        }
    }
    cursor_visible = false;
}

// İmleci back-buffer'dan kaldırıp eski alanı hemen ekrana aktarır.
void cursor_hide(void) {
    if (!cursor_visible) return;

    cursor_prepare_redraw();
    fb_blit_region(old_mouse_x, old_mouse_y, CURSOR_WIDTH, CURSOR_HEIGHT);
}

static void cursor_show_internal(bool blit) {
    // Koordinatları güncel fare konumuna eşitle
    old_mouse_x = mouse_x;
    old_mouse_y = mouse_y;

    // Yeni konumun arka planını kaydet
    for (int y = 0; y < CURSOR_HEIGHT; y++) {
        for (int x = 0; x < CURSOR_WIDTH; x++) {
            int px = old_mouse_x + x;
            int py = old_mouse_y + y;
            if (px >= 0 && (uint32_t)px < fb_get_width() && py >= 0 && (uint32_t)py < fb_get_height()) {
                cursor_bg_buffer[y * CURSOR_WIDTH + x] = fb_getpixel(px, py);
            }
        }
    }

    // Yeni konuma imleci çiz
    for (int y = 0; y < CURSOR_HEIGHT; y++) {
        for (int x = 0; x < CURSOR_WIDTH; x++) {
            int px = old_mouse_x + x;
            int py = old_mouse_y + y;
            if (px >= 0 && (uint32_t)px < fb_get_width() && py >= 0 && (uint32_t)py < fb_get_height()) {
                fb_putpixel(px, py, 0xFFFFFFFF); // Beyaz imleç
            }
        }
    }
    if (blit) {
        fb_blit_region(old_mouse_x, old_mouse_y, CURSOR_WIDTH, CURSOR_HEIGHT);
    }
    cursor_visible = true;
}

void cursor_show(void) {
    old_mouse_x = mouse_x;
    old_mouse_y = mouse_y;

    cursor_show_internal(true);
}

void cursor_update_and_redraw(void) {
    // Önce güncel fare konumunu kontrol et (mouse_x ve mouse_y değişti mi?)
    if (mouse_x == old_mouse_x && mouse_y == old_mouse_y) {
        return;
    }

    int32_t old_x = old_mouse_x;
    int32_t old_y = old_mouse_y;

    // 1. Eski imleci back-buffer'dan ekrana geri yükle (eski izi sil)
    cursor_prepare_redraw();

    // 2. Yeni koordinatları senkronize et
    old_mouse_x = mouse_x;
    old_mouse_y = mouse_y;

    // 3. Yeni konumun arka planını kaydet ve imleci yeni konuma çiz (blit yapmadan)
    cursor_show_internal(false);

    // 4. Eski ve yeni alanı kapsayan bölgeyi tek seferde ekrana blit et
    int32_t blit_x = (old_x < mouse_x) ? old_x : mouse_x;
    int32_t blit_y = (old_y < mouse_y) ? old_y : mouse_y;
    int32_t right = ((old_x + CURSOR_WIDTH) > (mouse_x + CURSOR_WIDTH))
        ? old_x + CURSOR_WIDTH : mouse_x + CURSOR_WIDTH;
    int32_t bottom = ((old_y + CURSOR_HEIGHT) > (mouse_y + CURSOR_HEIGHT))
        ? old_y + CURSOR_HEIGHT : mouse_y + CURSOR_HEIGHT;

    fb_blit_region(blit_x, blit_y, right - blit_x, bottom - blit_y);
}

// İmlecin altındaki arka planı o anki ekrandan yeniden okuyarak günceller (hayalet görüntüleri önler)
void cursor_refresh_background(void) {
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

void cursor_sync_position(void) {
    old_mouse_x = mouse_x;
    old_mouse_y = mouse_y;
}