#include <ui/desktop.h>
#include <ui/window.h>
#include <kernel/drivers/video/fb.h>
#include <kernel/drivers/video/gfx.h>

#define TASKBAR_HEIGHT 40

void desktop_init(void) {
    uint32_t width = fb_get_width();
    uint32_t height = fb_get_height();

    if (width == 0 || height == 0) return;

    // 1. Görev Çubuğu Arkaplanı
    uint32_t taskbar_y = height - TASKBAR_HEIGHT;
    gfx_fill_rect(0, taskbar_y, width, TASKBAR_HEIGHT, 0xFF202020);

    // 2. Görev Çubuğu Üst Çizgisi
    gfx_fill_rect(0, taskbar_y, width, 2, 0xFF404040);

    // 3. Sol tarafa "Menü / Başlat" butonu
    gfx_fill_rect(10, taskbar_y + 8, 24, 24, 0xFF007ACC);

    // Başlat butonunun yanına yazı ekleyelim
    gfx_draw_text_utf8(42, taskbar_y + 12, 0xFFFFFFFF, "KryonOS");

    gfx_draw_text_utf8(50, 50, 0xFFFFFFFF, "ş s ğ g ü u ı i | Ş S Ğ G Ü U İ I");

    // --- ÖRNEK PENCERE OLUŞTURMA VE ÇİZME ---
    window_t sample_win;
    sample_win.x = 100;
    sample_win.y = 80;
    sample_win.width = 300;
    sample_win.height = 200;
    sample_win.is_active = true; // Aktif pencere (mavi başlık çubuğu)
    sample_win.is_dragging = false;

    // Başlık metnini kopyala (Türkçe karakter testine uygun UTF-8/KVX uyumlu)
    const char* win_title = "KryonOS Dosya Yöneticisi";
    int i = 0;
    while (win_title[i] != '\0' && i < 31) {
        sample_win.title[i] = win_title[i];
        i++;
    }
    sample_win.title[i] = '\0';

    // Pencereyi ekrana çiz
    window_draw(&sample_win);
}