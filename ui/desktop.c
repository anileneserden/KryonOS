#include <ui/desktop.h>
#include <ui/wm.h>
#include <ui/cursor.h>
#include <kernel/drivers/video/fb.h>
#include <kernel/drivers/video/gfx.h>
#include <kernel/drivers/input/keyboard_ps2.h>
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

    uint32_t sw = fb_get_width();
    uint32_t sh = fb_get_height();

    // Arka plan rengi ve pencereler
    gfx_fill_rect(screen_damage.x, screen_damage.y, screen_damage.w, screen_damage.h, 0xFF0000FF);
    wm_draw_all();

    // Alt Görev Çubuğu (Taskbar)
    gfx_fill_rect(0, sh - 32, sw, 32, 0xFF1E1E1E); 
    gfx_fill_rect(0, sh - 32, sw, 1, 0xFF333333);  

    gfx_fill_rect(4, sh - 28, 65, 24, 0xFF007ACC);
    gfx_draw_text_utf8(10, sh - 22, 0xFFFFFFFF, "Baslat");

    fb_blit_region(screen_damage.x, screen_damage.y, screen_damage.w, screen_damage.h);
    damage_clear();
}

void desktop_init(void) {
    uint32_t width = fb_get_width();
    uint32_t height = fb_get_height();

    if (width == 0 || height == 0) return;

    wm_init();
    wm_create_window(300, 200, "KryonOS Dosya Yoneticisi");
    wm_create_window(250, 180, "Sistem Ayarlari");

    damage_union_rect(0, 0, width, height);
    desktop_redraw();
}

// Sadece klavye kontrolünü yöneten merkezi fonksiyon
void desktop_process_input(void) {
    uint8_t key = keyboard_get_last_scancode();
    if (key == 0x01) {
        serial_write("ESC tuşuna basıldı.\n");
        keyboard_clear_last_scancode();
    }
}