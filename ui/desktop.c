#include <ui/desktop.h>
#include <ui/window.h>
#include <kernel/drivers/video/fb.h>

#define TASKBAR_HEIGHT 40

void desktop_init(void) {
    uint32_t width = fb_get_width();
    uint32_t height = fb_get_height();

    if (width == 0 || height == 0) return;

    // 1. Görev Çubuğu Arkaplanı
    uint32_t taskbar_y = height - TASKBAR_HEIGHT;
    fb_draw_rect(0, taskbar_y, width, TASKBAR_HEIGHT, 0xFF202020);

    // 2. Görev Çubuğu Üst Çizgisi
    fb_draw_rect(0, taskbar_y, width, 2, 0xFF404040);

    // 3. Sol tarafa "Menü / Başlat" butonu
    fb_draw_rect(10, taskbar_y + 8, 24, 24, 0xFF007ACC);

    // --- ÖRNEK PENCERE OLUŞTURMA VE ÇİZME ---
    window_t sample_win;
    sample_win.x = 100;
    sample_win.y = 80;
    sample_win.width = 300;
    sample_win.height = 200;
    sample_win.is_active = true; // Aktif pencere (mavi başlık çubuğu)
    sample_win.is_dragging = false;

    // Başlık metnini kopyala
    const char* win_title = "KryonOS Dosya Yoneticisi";
    int i = 0;
    while (win_title[i] != '\0' && i < 31) {
        sample_win.title[i] = win_title[i];
        i++;
    }
    sample_win.title[i] = '\0';

    // Pencereyi ekrana çiz
    window_draw(&sample_win);
}