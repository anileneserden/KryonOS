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

    cursor_prepare_redraw();

    // 1. Masaüstü Arka Planı
    gfx_fill_rect(screen_damage.x, screen_damage.y, screen_damage.w, screen_damage.h, 0xFF1E1E1E);

    // 2. Ekran Boyunca Grid Çizgileri ve Çerçeve (0,0'dan ekran sınırlarına kadar)
    int screen_w = fb_get_width();
    int screen_h = fb_get_height();
    int cell_w = grid_get_cell_width();   // Örn: 64
    int cell_h = grid_get_cell_height(); // Örn: 64
    uint32_t grid_line_color = 0xFF2A2A2A; // Şık, koyu gri bir çizgi rengi (istersen beyaz yapabilirsin: 0xFFFFFFFF)

    // Dikey çizgileri çiz (0'dan screen_w'ye kadar)
    for (int x = 0; x <= screen_w; x += cell_w) {
        if (x >= screen_damage.x && x <= screen_damage.x + screen_damage.w) {
            gfx_fill_rect(x, screen_damage.y, 1, screen_damage.h, grid_line_color);
        }
    }

    // Yatay çizgileri çiz (0'dan screen_h'ye kadar)
    for (int y = 0; y <= screen_h; y += cell_h) {
        if (y >= screen_damage.y && y <= screen_damage.y + screen_damage.h) {
            gfx_fill_rect(screen_damage.x, y, screen_damage.w, 1, grid_line_color);
        }
    }

    // 3. Açık Pencereleri Çiz
    wm_draw_all();

    // 4. Backbuffer'dan VRAM'e aktar
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
    wm_create_window(350, 220, "KryonOS Pencere");

    damage_union_rect(0, 0, width, height);
    desktop_redraw();

    cursor_init();
    cursor_sync_position();
    cursor_show();
    cursor_refresh_background();
}

void desktop_process_input(void) {
    wm_process_input();
}