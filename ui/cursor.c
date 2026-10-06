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

static uint32_t cursor_bg_buffer[CURSOR_WIDTH * CURSOR_HEIGHT];
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
    
    // C:/Kryon/Cursors/arrow.png yolundan imleci yüklemeyi dene
    png_image_t* cursor_png = png_load_from_file("C:/Kryon/Cursors/arrow.png");
    if (cursor_png) {
        custom_cursor_pixels = cursor_png->pixels;
        cursor_img_width = cursor_png->width;
        cursor_img_height = cursor_png->height;
        free(cursor_png);
        serial_write("CURSOR: C:/Kryon/Cursors/arrow.png basariyla yuklendi!\n");
    } else {
        serial_write("CURSOR: C:/Kryon/Cursors/arrow.png bulunamadi, varsayilan imlec kullanilacak.\n");
    }

    int draw_w = get_draw_width();
    int draw_h = get_draw_height();

    // Arka planı güvenli bir şekilde kaydet
    for (int y = 0; y < draw_h; y++) {
        for (int x = 0; x < draw_w; x++) {
            int px = old_mouse_x + x;
            int py = old_mouse_y + y;
            if (px >= 0 && (uint32_t)px < fb_get_width() && py >= 0 && (uint32_t)py < fb_get_height()) {
                if (y < CURSOR_HEIGHT && x < CURSOR_WIDTH) {
                    cursor_bg_buffer[y * CURSOR_WIDTH + x] = fb_getpixel(px, py);
                }
            }
        }
    }
}

void cursor_get_position(int32_t* x, int32_t* y) {
    if (x) *x = old_mouse_x;
    if (y) *y = old_mouse_y;
}

// Remove the cursor from the back buffer before drawing the screen or windows.
void cursor_prepare_redraw(void) {
    if (!cursor_visible) return;

    int draw_w = get_draw_width();
    int draw_h = get_draw_height();

    for (int y = 0; y < draw_h; y++) {
        for (int x = 0; x < draw_w; x++) {
            int px = old_mouse_x + x;
            int py = old_mouse_y + y;
            if (px >= 0 && (uint32_t)px < fb_get_width() && py >= 0 && (uint32_t)py < fb_get_height()) {
                if (y < CURSOR_HEIGHT && x < CURSOR_WIDTH) {
                    fb_putpixel(px, py, cursor_bg_buffer[y * CURSOR_WIDTH + x]);
                }
            }
        }
    }
    cursor_visible = false;
}

// Remove the cursor from the back buffer and immediately blit its old area.
void cursor_hide(void) {
    if (!cursor_visible) return;

    cursor_prepare_redraw();
    fb_blit_region(old_mouse_x, old_mouse_y, get_draw_width(), get_draw_height());
}

void cursor_show_internal(bool blit) {
    // Synchronize the coordinates with the current mouse position
    old_mouse_x = mouse_x;
    old_mouse_y = mouse_y;

    int draw_w = get_draw_width();
    int draw_h = get_draw_height();

    // Save the background at the new position
    for (int y = 0; y < draw_h; y++) {
        for (int x = 0; x < draw_w; x++) {
            int px = old_mouse_x + x;
            int py = old_mouse_y + y;
            if (px >= 0 && (uint32_t)px < fb_get_width() && py >= 0 && (uint32_t)py < fb_get_height()) {
                if (y < CURSOR_HEIGHT && x < CURSOR_WIDTH) {
                    cursor_bg_buffer[y * CURSOR_WIDTH + x] = fb_getpixel(px, py);
                }
            }
        }
    }

    // Draw the cursor at the new position (PNG pikselleri veya şeffaflık kontrolü ile)
    for (int y = 0; y < draw_h; y++) {
        for (int x = 0; x < draw_w; x++) {
            int px = old_mouse_x + x;
            int py = old_mouse_y + y;
            if (px >= 0 && (uint32_t)px < fb_get_width() && py >= 0 && (uint32_t)py < fb_get_height()) {
                
                if (custom_cursor_pixels && x < cursor_img_width && y < cursor_img_height) {
                    uint32_t pixel = custom_cursor_pixels[y * cursor_img_width + x];
                    uint8_t alpha = (pixel >> 24) & 0xFF; // RGBA alpha kanalı
                    
                    // Tamamen şeffaf değilse pikseli çiz (alpha eşiği > 10)
                    if (alpha > 10) {
                        fb_putpixel(px, py, pixel);
                    }
                } else {
                    // Fallback: PNG yüklenemediyse klasik beyaz imleç
                    fb_putpixel(px, py, 0xFFFFFFFF); 
                }
            }
        }
    }
    
    if (blit) {
        fb_blit_region(old_mouse_x, old_mouse_y, draw_w, draw_h);
    }
    cursor_visible = true;
}

void cursor_show(void) {
    old_mouse_x = mouse_x;
    old_mouse_y = mouse_y;

    cursor_show_internal(true);
}

extern uint8_t mouse_buttons; // Button state from mouse_ps2.c
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

    if (position_changed) {
        damage_union_rect(old_mouse_x, old_mouse_y, draw_w, draw_h);
        damage_union_rect(mouse_x, mouse_y, draw_w, draw_h);
    } else if (buttons_changed) {
        damage_union_rect(old_mouse_x, old_mouse_y, draw_w, draw_h);
    }

    desktop_redraw();
}

// Refresh the background beneath the cursor from the current screen (prevents ghosting)
void cursor_refresh_background(void) {
    int draw_w = get_draw_width();
    int draw_h = get_draw_height();

    for (int y = 0; y < draw_h; y++) {
        for (int x = 0; x < draw_w; x++) {
            int px = old_mouse_x + x;
            int py = old_mouse_y + y;
            if (px >= 0 && (uint32_t)px < fb_get_width() && py >= 0 && (uint32_t)py < fb_get_height()) {
                if (y < CURSOR_HEIGHT && x < CURSOR_WIDTH) {
                    cursor_bg_buffer[y * CURSOR_WIDTH + x] = fb_getpixel(px, py);
                }
            }
        }
    }
}

void cursor_sync_position(void) {
    old_mouse_x = mouse_x;
    old_mouse_y = mouse_y;
}

int32_t cursor_get_x(void) {
    return mouse_x;
}

int32_t cursor_get_y(void) {
    return mouse_y;
}

int32_t cursor_get_old_x(void) {
    return old_mouse_x;
}

int32_t cursor_get_old_y(void) {
    return old_mouse_y;
}

int32_t cursor_get_width(void) {
    return get_draw_width();
}

int32_t cursor_get_height(void) {
    return get_draw_height();
}