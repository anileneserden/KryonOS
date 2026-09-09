#include <ui/desktop.h>
#include <ui/wm.h>
#include <kernel/drivers/video/fb.h>
#include <kernel/drivers/video/gfx.h>

void desktop_init(void) {
    uint32_t width = fb_get_width();
    uint32_t height = fb_get_height();

    if (width == 0 || height == 0) return;

    // 1. Pencere yöneticisini başlat
    wm_init();

    // 2. Test penceresi oluştur (otomatik konumlandırma ile)
    wm_create_window(300, 200, "KryonOS Dosya Yoneticisi");
    wm_create_window(250, 180, "Sistem Ayarlari");
}