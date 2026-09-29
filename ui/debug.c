#include <ui/debug.h>
#include <kernel/drivers/video/gfx.h>
#include <kernel/drivers/video/fb.h>

// Kare özellikleri
static int square_x = 10;
static const int square_y = 100;
static const int square_size = 50;
static const int square_speed = 3;
static const uint32_t square_color = 0x0000FF00; // Yeşil kare (0xRRGGBB)
static const uint32_t bg_color = 0x00000000;     // Siyah arkaplan

void debug_screen_init(void) {
    // Ekranı komple siyah yap
    gfx_fill_screen(bg_color);
    gfx_flush();
}

void debug_screen_update(void) {
    // 1. Ekran boyutlarını al
    uint32_t screen_w = fb_get_width();

    // 2. Önceki kareyi silmek için ekranı temizle (veya sadece karenin izini temizle)
    gfx_fill_screen(bg_color);

    // 3. Yeni konumda kareyi çiz (x, y, w, h, color)
    gfx_fill_rect(square_x, square_y, square_size, square_size, square_color);

    // 4. Konumu güncelle (Ekranın sağından çıkarsa sola dönsün)
    square_x += square_speed;
    if (square_x > (int)screen_w) {
        square_x = -square_size;
    }

    // 5. Çift tamponlama (double buffer) varsa VRAM'e kopyala
    gfx_flush();
}