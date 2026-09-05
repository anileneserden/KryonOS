#include <ui/window.h>
#include <kernel/drivers/video/fb.h>

void window_draw(window_t* win) {
    if (!win) return;

    // 1. Pencere gövdesi (Arka plan: Beyaz / Gri tonları)
    fb_draw_rect(win->x, win->y, win->width, win->height, 0xFFE0E0E0);

    // 2. Başlık Çubuğu (Aktif durumuna göre renk değişimi: Aktifse koyu mavi, pasifse gri)
    uint32_t title_bar_color = win->is_active ? 0xFF005A9E : 0xFF606060;
    fb_draw_rect(win->x, win->y, win->width, WINDOW_TITLE_HEIGHT, title_bar_color);

    // 3. Pencere Çerçevesi (Estetik sınır çizgisi)
    // Üst, sol, sağ ve alt kenar çizgileri
    fb_draw_rect(win->x, win->y, win->width, 2, 0xFF808080); // Üst sınır
    fb_draw_rect(win->x, win->y + win->height - 2, win->width, 2, 0xFF505050); // Alt sınır
    fb_draw_rect(win->x, win->y, 2, win->height, 0xFF808080); // Sol sınır
    fb_draw_rect(win->x + win->width - 2, win->y, 2, win->height, 0xFF505050); // Sağ sınır

    // 4. Kapatma Butonu (Başlık çubuğunun sağ üst köşesinde küçük kırmızı bir kare)
    uint32_t btn_size = 14;
    uint32_t btn_x = win->x + win->width - btn_size - 5;
    uint32_t btn_y = win->y + (WINDOW_TITLE_HEIGHT - btn_size) / 2;
    fb_draw_rect(btn_x, btn_y, btn_size, btn_size, 0xFFE81123);
}