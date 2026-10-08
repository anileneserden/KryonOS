#include <ui/cursor.h>
#include <kernel/drivers/input/mouse_ps2.h>
#include <kernel/drivers/video/fb.h>
#include <kernel/loader/png.h>
#include <kernel/serial.h>
#include <stdlib.h>
#include <stdbool.h>

static int32_t old_mouse_x = 400;
static int32_t old_mouse_y = 300;

// Özel PNG imleci için değişkenler
static uint32_t* custom_cursor_pixels = NULL;
static int cursor_img_width = 0;
static int cursor_img_height = 0;

// Otomatik tespit edilecek dinamik hotspot değişkenleri
static int hotspot_x = 0;
static int hotspot_y = 0;

static uint32_t* cursor_bg_buffer = NULL;
static int bg_buffer_width = 0;
static int bg_buffer_height = 0;
static bool cursor_visible = true;

extern void fb_blit_region(uint32_t x, uint32_t y, uint32_t width, uint32_t height);

// Aktif çizim genişliğini ve yüksekliğini belirleyen yardımcı fonksiyon
static inline int get_draw_width(void) {
    return (custom_cursor_pixels && cursor_img_width > 0) ? cursor_img_width : CURSOR_WIDTH;
}

static inline int get_draw_height(void) {
    return (custom_cursor_pixels && cursor_img_height > 0) ? cursor_img_height : CURSOR_HEIGHT;
}

void cursor_init(void) {
    old_mouse_x = mouse_x;
    old_mouse_y = mouse_y;
    cursor_visible = false;
    
    png_image_t* cursor_png = png_load_from_file("C:/Kryon/Cursors/arrow.png");
    if (cursor_png) {
        custom_cursor_pixels = cursor_png->pixels;
        cursor_img_width = cursor_png->width;
        cursor_img_height = cursor_png->height;

        // --- OTOMATİK HOTSPOT TESPİTİ (İLK GÖRÜNÜR PİKSELİ BUL) ---
        bool found = false;
        for (int y = 0; y < cursor_img_height; y++) {
            for (int x = 0; x < cursor_img_width; x++) {
                uint32_t pixel = custom_cursor_pixels[y * cursor_img_width + x];
                uint8_t alpha = (pixel >> 24) & 0xFF; // RGBA Alpha kanalı
                
                // Şeffaf olmayan (görünür) ilk pikseli bulduğumuzda dur
                if (alpha > 10) { 
                    hotspot_x = x;
                    hotspot_y = y;
                    found = true;
                    break;
                }
            }
            if (found) break;
        }

        free(cursor_png);
        serial_write("CURSOR: C:/Kryon/Cursors/arrow.png basariyla yuklendi ve otomatik hotspot hesaplandi!\n");
    } else {
        serial_write("CURSOR: C:/Kryon/Cursors/arrow.png bulunamadi, varsayilan imlec kullanilacak.\n");
        hotspot_x = 0;
        hotspot_y = 0;
    }

    int draw_w = get_draw_width();
    int draw_h = get_draw_height();

    // Arka plan tamponunu dinamik olarak boyutlandır
    if (cursor_bg_buffer) {
        free(cursor_bg_buffer);
    }
    cursor_bg_buffer = malloc(draw_w * draw_h * sizeof(uint32_t));
    bg_buffer_width = draw_w;
    bg_buffer_height = draw_h;

    int render_x = old_mouse_x - hotspot_x;
    int render_y = old_mouse_y - hotspot_y;

    // Arka planı güvenli bir şekilde kaydet
    for (int y = 0; y < draw_h; y++) {
        for (int x = 0; x < draw_w; x++) {
            int px = render_x + x;
            int py = render_y + y;
            if (px >= 0 && (uint32_t)px < fb_get_width() && py >= 0 && (uint32_t)py < fb_get_height()) {
                cursor_bg_buffer[y * draw_w + x] = fb_getpixel(px, py);
            } else {
                cursor_bg_buffer[y * draw_w + x] = 0;
            }
        }
    }
}

void cursor_get_position(int32_t* x, int32_t* y) {
    if (x) *x = old_mouse_x;
    if (y) *y = old_mouse_y;
}

void cursor_prepare_redraw(void) {
    if (!cursor_visible || !cursor_bg_buffer) return;

    int draw_w = get_draw_width();
    int draw_h = get_draw_height();
    int render_x = old_mouse_x - hotspot_x;
    int render_y = old_mouse_y - hotspot_y;

    for (int y = 0; y < draw_h; y++) {
        for (int x = 0; x < draw_w; x++) {
            int px = render_x + x;
            int py = render_y + y;
            if (px >= 0 && (uint32_t)px < fb_get_width() && py >= 0 && (uint32_t)py < fb_get_height()) {
                if (y < bg_buffer_height && x < bg_buffer_width) {
                    fb_putpixel(px, py, cursor_bg_buffer[y * bg_buffer_width + x]);
                }
            }
        }
    }
    cursor_visible = false;
}

void cursor_hide(void) {
    if (!cursor_visible) return;

    cursor_prepare_redraw();
    fb_blit_region(old_mouse_x - hotspot_x, old_mouse_y - hotspot_y, get_draw_width(), get_draw_height());
}

void cursor_show_internal(bool blit) {
    old_mouse_x = mouse_x;
    old_mouse_y = mouse_y;

    int draw_w = get_draw_width();
    int draw_h = get_draw_height();
    int render_x = old_mouse_x - hotspot_x;
    int render_y = old_mouse_y - hotspot_y;

    // Arka planı yeni konumda kaydet
    for (int y = 0; y < draw_h; y++) {
        for (int x = 0; x < draw_w; x++) {
            int px = render_x + x;
            int py = render_y + y;
            if (px >= 0 && (uint32_t)px < fb_get_width() && py >= 0 && (uint32_t)py < fb_get_height()) {
                if (y < bg_buffer_height && x < bg_buffer_width) {
                    cursor_bg_buffer[y * bg_buffer_width + x] = fb_getpixel(px, py);
                }
            }
        }
    }

    // İmleci yeni konumda çiz
    for (int y = 0; y < draw_h; y++) {
        for (int x = 0; x < draw_w; x++) {
            int px = render_x + x;
            int py = render_y + y;
            if (px >= 0 && (uint32_t)px < fb_get_width() && py >= 0 && (uint32_t)py < fb_get_height()) {
                
                if (custom_cursor_pixels && x < cursor_img_width && y < cursor_img_height) {
                    uint32_t pixel = custom_cursor_pixels[y * cursor_img_width + x];
                    uint8_t alpha = (pixel >> 24) & 0xFF; 
                    
                    if (alpha > 10) {
                        fb_putpixel(px, py, pixel);
                    }
                } else {
                    fb_putpixel(px, py, 0xFFFFFFFF); 
                }
            }
        }
    }
    
    if (blit) {
        fb_blit_region(render_x, render_y, draw_w, draw_h);
    }
    cursor_visible = true;
}

void cursor_show(void) {
    old_mouse_x = mouse_x;
    old_mouse_y = mouse_y;
    cursor_show_internal(true);
}

extern uint8_t mouse_buttons; 
static uint8_t old_mouse_buttons = 0;
extern void damage_union_rect(int x, int y, int w, int h);
extern void desktop_redraw(void);

void cursor_update_and_redraw(void) {
    bool position_changed = (mouse_x != old_mouse_x || mouse_y != old_mouse_y);
    bool buttons_changed = (mouse_buttons != old_mouse_buttons);

    if (!position_changed && !buttons_changed) {
        return;
    }

    old_mouse_buttons = mouse_buttons;

    int draw_w = get_draw_width();
    int draw_h = get_draw_height();
    int old_render_x = old_mouse_x - hotspot_x;
    int old_render_y = old_mouse_y - hotspot_y;
    int new_render_x = mouse_x - hotspot_x;
    int new_render_y = mouse_y - hotspot_y;

    if (position_changed) {
        damage_union_rect(old_render_x, old_render_y, draw_w, draw_h);
        damage_union_rect(new_render_x, new_render_y, draw_w, draw_h);
    } else if (buttons_changed) {
        damage_union_rect(old_render_x, old_render_y, draw_w, draw_h);
    }

    desktop_redraw();
}

void cursor_refresh_background(void) {
    if (!cursor_bg_buffer) return;
    int draw_w = get_draw_width();
    int draw_h = get_draw_height();
    int render_x = old_mouse_x - hotspot_x;
    int render_y = old_mouse_y - hotspot_y;

    for (int y = 0; y < draw_h; y++) {
        for (int x = 0; x < draw_w; x++) {
            int px = render_x + x;
            int py = render_y + y;
            if (px >= 0 && (uint32_t)px < fb_get_width() && py >= 0 && (uint32_t)px < fb_get_height()) {
                if (y < bg_buffer_height && x < bg_buffer_width) {
                    cursor_bg_buffer[y * bg_buffer_width + x] = fb_getpixel(px, py);
                }
            }
        }
    }
}

void cursor_sync_position(void) {
    old_mouse_x = mouse_x;
    old_mouse_y = mouse_y;
}

int32_t cursor_get_x(void) { return mouse_x; }
int32_t cursor_get_y(void) { return mouse_y; }
int32_t cursor_get_old_x(void) { return old_mouse_x; }
int32_t cursor_get_old_y(void) { return old_mouse_y; }
int32_t cursor_get_width(void) { return get_draw_width(); }
int32_t cursor_get_height(void) { return get_draw_height(); }