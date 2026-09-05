#include <ui/desktop.h>
#include <kernel/drivers/video/fb.h>

#define TASKBAR_HEIGHT 40

void desktop_init(void) {
    uint32_t width = fb_get_width();
    uint32_t height = fb_get_height();

    if (width == 0 || height == 0) return;

    // 1. Masaüstü arkaplanı (Zaten kmain içinde mavi yapılmıştı ama ana renk olarak kalabilir)
    
    // 2. Görev Çubuğu Arkaplanı (Koyu Gri / Füme: Örn: 0xFF202020)
    uint32_t taskbar_y = height - TASKBAR_HEIGHT;
    fb_draw_rect(0, taskbar_y, width, TASKBAR_HEIGHT, 0xFF202020);

    // 3. Görev Çubuğu Üst Çizgisi (Estetik bir sınır çizgisi: Açık Gri / Accent)
    fb_draw_rect(0, taskbar_y, width, 2, 0xFF404040);

    // 4. Sol tarafa örnek bir "Menü / Başlat" butonu (Örn: Mavi/Turkuaz renkli küçük kare)
    fb_draw_rect(10, taskbar_y + 8, 24, 24, 0xFF007ACC);
}