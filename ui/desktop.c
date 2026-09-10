#include <ui/desktop.h>
#include <ui/wm.h>
#include <kernel/drivers/video/fb.h>
#include <kernel/drivers/video/gfx.h>

typedef struct {
    int x, y, w, h;
    bool active;
} damage_rect_t;

static damage_rect_t screen_damage = {0, 0, 0, 0};

void damage_clear(void) {
    screen_damage.active = false;
    screen_damage.x = 0;
    screen_damage.y = 0;
    screen_damage.w = 0;
    screen_damage.h = 0;
}

void damage_union_rect(int x, int y, int w, int h) {
    int sw = fb_get_width();
    int sh = fb_get_height();

    // Ekran sınırları dışına taşmayı kırp (clip)
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > sw) w = sw - x;
    if (y + h > sh) h = sh - y;
    if (w <= 0 || h <= 0) return;

    if (!screen_damage.active) {
        screen_damage.x = x;
        screen_damage.y = y;
        screen_damage.w = w;
        screen_damage.h = h;
        screen_damage.active = true;
    } else {
        // Mevcut hasar alanı ile yeni alanı birleştir (Bounding Box)
        int min_x = (screen_damage.x < x) ? screen_damage.x : x;
        int min_y = (screen_damage.y < y) ? screen_damage.y : y;
        int max_x = ((screen_damage.x + screen_damage.w) > (x + w)) ? (screen_damage.x + screen_damage.w) : (x + w);
        int max_y = ((screen_damage.y + screen_damage.h) > (y + h)) ? (screen_damage.y + screen_damage.h) : (y + h);

        screen_damage.x = min_x;
        screen_damage.y = min_y;
        screen_damage.w = max_x - min_x;
        screen_damage.h = max_y - min_y;
    }
}

void desktop_redraw(void) {
    if (!screen_damage.active) return;

    // 1. Sadece hasarlı alanı arka plan rengiyle (mavi) doldur
    gfx_fill_rect(screen_damage.x, screen_damage.y, screen_damage.w, screen_damage.h, 0xFF0000FF);

    // 2. Pencereleri çiz
    wm_draw_all();

    // 3. Sadece hasarlı bölgeyi ekrana aktar (Blit)
    fb_blit_region(screen_damage.x, screen_damage.y, screen_damage.w, screen_damage.h);

    // 4. Hasarı temizle
    damage_clear();
}

void desktop_init(void) {
    uint32_t width = fb_get_width();
    uint32_t height = fb_get_height();

    if (width == 0 || height == 0) return;

    // 1. Pencere yöneticisini başlat
    wm_init();

    // 2. Test pencereleri oluştur
    wm_create_window(300, 200, "KryonOS Dosya Yoneticisi");
    wm_create_window(250, 180, "Sistem Ayarlari");

    // İlk açılışta tüm ekranı hasarlı kabul edip çizelim
    damage_union_rect(0, 0, width, height);
    desktop_redraw();
}