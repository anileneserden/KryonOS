#include <ui/window.h>
#include <kernel/drivers/video/gfx.h>

void window_draw(window_t* win) {
    if (!win) return;

    int radius = 8; // Pencere köşelerinin yumuşaklık yarıçapı

    // 1. Pencere gövdesi (Yuvarlatılmış arka plan)
    gfx_fill_rounded_rect(win->x, win->y, win->width, win->height, radius, 0xFFE0E0E0);

    // 2. Başlık Çubuğu (Üst köşeleri yuvarlatılmış, alt köşeleri düz olan alan için veya komple üst kısım)
    uint32_t title_bar_color = win->is_active ? 0xFF005A9E : 0xFF606060;
    gfx_fill_rect(win->x, win->y, win->width, WINDOW_TITLE_HEIGHT, title_bar_color);

    // 3. Kapatma Butonu (Başlık çubuğunun sağ üst köşesinde küçük kırmızı bir kare)
    uint32_t btn_size = 14;
    uint32_t btn_x = win->x + win->width - btn_size - 6;
    uint32_t btn_y = win->y + (WINDOW_TITLE_HEIGHT - btn_size) / 2;
    gfx_fill_rect(btn_x, btn_y, btn_size, btn_size, 0xFFE81123);
}