#include <ui/desktop.h>
#include <ui/wm.h>
#include <ui/cursor.h>
#include <ui/grid.h> // Grid başlığını eklemeyi unutma
#include <kernel/drivers/video/fb.h>
#include <kernel/drivers/video/gfx.h>
#include <kernel/drivers/input/keyboard_ps2.h>
#include <arch/x86/io.h>

extern uint8_t mouse_buttons;

typedef struct {
    int x, y, w, h;
    bool active;
} damage_rect_t;

static damage_rect_t screen_damage = {0, 0, 0, 0, false};

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

    // 1. Önce eski imleci kaldır (arkasındaki pikselleri geri yükle)
    cursor_prepare_redraw();

    // 2. Masaüstü Arka Planı
    gfx_fill_rect(screen_damage.x, screen_damage.y, screen_damage.w, screen_damage.h, 0xFF1E1E1E);

    // 3. Grid Çizgileri
    int screen_w = fb_get_width();
    int screen_h = fb_get_height();
    int cell_w = grid_get_cell_width();
    int cell_h = grid_get_cell_height();
    uint32_t grid_line_color = 0xFF2A2A2A;

    for (int x = 0; x <= screen_w; x += cell_w) {
        if (x >= screen_damage.x && x <= screen_damage.x + screen_damage.w) {
            gfx_fill_rect(x, screen_damage.y, 1, screen_damage.h, grid_line_color);
        }
    }

    for (int y = 0; y <= screen_h; y += cell_h) {
        if (y >= screen_damage.y && y <= screen_damage.y + screen_damage.w) { // (Küçük düzeltme: screen_damage.w olmalı)
            gfx_fill_rect(screen_damage.x, y, screen_damage.w, 1, grid_line_color);
        }
    }

    // 4. Açık Pencereleri Çiz
    wm_draw_all();

    // 5. Alt Görev Çubuğu (Taskbar) - En üst katmanda (Pencerelerin üzerinde) çizilir
    int taskbar_h = 36;
    int taskbar_y = screen_h - taskbar_h;
    if (screen_damage.y + screen_damage.h >= taskbar_y) {
        // Çubuğun arka planı (Koyu gri / siyah tonu)
        gfx_fill_rect(screen_damage.x, taskbar_y > screen_damage.y ? taskbar_y : screen_damage.y, 
                      screen_damage.w, taskbar_h, 0xFF181818);
        // Çubuğun üst çizgisi (Modern bir border efekti için ince açık çizgi)
        gfx_fill_rect(screen_damage.x, taskbar_y, screen_damage.w, 1, 0xFF333333);
    }

    // 6. İmleci backbuffer'daki yeni yerine çiz (blit=false, çünkü toplu blit yapacağız)
    cursor_show_internal(false);

    // 7. Hasarlı bölgenin tamamını ekrana aktar (Blit)
    fb_blit_region(screen_damage.x, screen_damage.y, screen_damage.w, screen_damage.h);
    
    damage_clear();
}

void desktop_init(void) {
    uint32_t width = fb_get_width();
    uint32_t height = fb_get_height();

    if (width == 0 || height == 0) return;

    // Grid sistemini burada ekran boyutlarıyla başlatıyoruz (örn: 64x64 hücre boyutu)
    grid_init(width, height, 100, 100);

    wm_init();
<<<<<<< HEAD
    wm_create_window(350, 220, "KryonOS Pencere");
=======
    // wm_create_window(300, 200, "KryonOS Dosya Yoneticisi");
    // wm_create_window(250, 180, "Sistem Ayarlari");
>>>>>>> 54273fa (kef: fix string offset translation and integrate MediaPlayer application)

    damage_union_rect(0, 0, width, height);
    desktop_redraw();

    cursor_init();
    cursor_sync_position();
    cursor_show();
    cursor_refresh_background();
}

void desktop_process_input(void) {
    // 1. Önceki imleç konumunu al (Eğer cursor modülün bu veriyi tutuyorsa)
    int old_x = cursor_get_old_x();
    int old_y = cursor_get_old_y();

    // 2. Girdi ve pencere olaylarını işle (fare/klavye konumları güncellenir)
    wm_process_input();
    // Veya ps2 mouse paketleri burada işleniyorsa imleç yeni konumuna geçer

    int new_x = cursor_get_x();
    int new_y = cursor_get_y();
    int cursor_w = cursor_get_width();   // Örn: imleç genişliği (12-16px)
    int cursor_h = cursor_get_height();  // Örn: imleç yüksekliği

    // 3. Eğer imleç yer değiştirdiyse, eski ve yeni yerini kirli bölge ilan et
    if (old_x != new_x || old_y != new_y) {
        damage_union_rect(old_x, old_y, cursor_w, cursor_h); // Eski yerini temizle
        damage_union_rect(new_x, new_y, cursor_w, cursor_h); // Yeni yerini çiz
        
        // Yeniden çizimi tetikle
        desktop_redraw();
    }
}