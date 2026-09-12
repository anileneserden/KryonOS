#include <ui/desktop.h>
#include <ui/desktop_icons.h>
#include <ui/wm.h>
#include <ui/cursor.h>
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
static bool prev_mouse_buttons = false;

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

    cursor_prepare_redraw();

    // 1. Masaüstü Arka Planı
    gfx_fill_rect(screen_damage.x, screen_damage.y, screen_damage.w, screen_damage.h, 0xFF0000FF);
    
    int32_t mx, my;
    cursor_get_position(&mx, &my);
    bool left_clicked = (mouse_buttons & 1);
    bool click_started = left_clicked && !prev_mouse_buttons; 
    prev_mouse_buttons = left_clicked;

    // 2. İkonları Yönet ve Çiz
    desktop_icons_draw(mx, my, click_started);

    // 3. Açık Pencereleri Çiz
    wm_draw_all();

    fb_blit_region(screen_damage.x, screen_damage.y, screen_damage.w, screen_damage.h);
    damage_clear();
}

void desktop_init(void) {
    uint32_t width = fb_get_width();
    uint32_t height = fb_get_height();

    if (width == 0 || height == 0) return;

    desktop_icons_init(); // İkon modülünü başlat

    wm_init();
    wm_create_window(300, 200, "KryonOS Dosya Yoneticisi");
    wm_create_window(250, 180, "Sistem Ayarlari");

    damage_union_rect(0, 0, width, height);
    desktop_redraw();

    cursor_init();
    cursor_sync_position();
    cursor_show();
    cursor_refresh_background();
}

void desktop_process_input(void) {
}