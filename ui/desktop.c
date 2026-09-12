#include <ui/desktop.h>
#include <ui/wm.h>
#include <ui/cursor.h>
#include <kernel/drivers/video/fb.h>
#include <kernel/drivers/video/gfx.h>
#include <kernel/drivers/input/keyboard_ps2.h>
#include <kernel/fs/vfs.h>
#include <arch/x86/io.h>
#include <kernel/serial.h>

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

    // Çizim başlamadan önce varsa eski imleci temizle
    cursor_prepare_redraw();

    uint32_t sw = fb_get_width();
    uint32_t sh = fb_get_height();

    // 1. Masaüstü Arka Planı (Koyu mavi/gri ton)
    gfx_fill_rect(screen_damage.x, screen_damage.y, screen_damage.w, screen_damage.h, 0xFF0000FF);
    
    // 1.1 VFS Üzerinden Belirtilen Klasördeki Dosyaları Okuyup Masaüstü İkonları Olarak Çiz
    vfs_file_info_t files[16];
    int file_count = vfs_get_directory_files("C:/Users/anil/Desktop/", files, 16);

    int icon_x = 30;
    int icon_y = 30;

    for (int i = 0; i < file_count; i++) {
        // İkon Arka Plan Kutusu (Dosya simgesi efekti)
        gfx_fill_rect(icon_x, icon_y, 40, 40, 0xFFE0E0E0);
        gfx_fill_rect(icon_x + 4, icon_y + 4, 32, 28, 0xFFFFFFFF);

        // Dosya Adı Etiketi
        gfx_draw_text_utf8(icon_x - 4, icon_y + 45, 0xFFFFFFFF, files[i].name);

        // Sonraki ikon için dikeyde aşağı kaydır
        icon_y += 70;
    }

    // 2. Açık Pencereleri Çiz
    wm_draw_all();

    fb_blit_region(screen_damage.x, screen_damage.y, screen_damage.w, screen_damage.h);
    damage_clear();
}

void desktop_init(void) {
    uint32_t width = fb_get_width();
    uint32_t height = fb_get_height();

    if (width == 0 || height == 0) return;

    // Window Manager'ı başlat ve örnek pencereler oluştur
    wm_init();
    wm_create_window(300, 200, "KryonOS Dosya Yoneticisi");
    wm_create_window(250, 180, "Sistem Ayarlari");

    // Ekranı hasarlı işaretleyip ilk çizimi tetikle
    damage_union_rect(0, 0, width, height);
    desktop_redraw();

    // İmleci başlat ve konumunu mühürle
    cursor_init();
    cursor_sync_position();
    cursor_show();
    cursor_refresh_background();
}

void desktop_process_input(void) {
    // Klavye veya genel masaüstü kısayolları buraya eklenebilir
}