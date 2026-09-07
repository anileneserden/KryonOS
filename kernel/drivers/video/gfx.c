/* kernel/drivers/video/gfx.c */

#include <kernel/drivers/video/gfx.h>
#include <kernel/drivers/video/fb.h>

void gfx_draw_pixel(int x, int y, uint32_t color) {
    fb_putpixel(x, y, color);
}

void gfx_fill_rect(int x, int y, int width, int height, uint32_t color) {
    for (int cy = 0; cy < height; cy++) {
        for (int cx = 0; cx < width; cx++) {
            gfx_draw_pixel(x + cx, y + cy, color);
        }
    }
}

void gfx_fill_rounded_rect(int x, int y, int width, int height, int radius, uint32_t color) {
    for (int cy = 0; cy < height; cy++) {
        for (int cx = 0; cx < width; cx++) {
            int draw_pixel = 1;

            // Sol-Üst Köşe
            if (cx < radius && cy < radius) {
                int dx = radius - cx;
                int dy = radius - cy;
                if ((dx * dx + dy * dy) > (radius * radius)) {
                    draw_pixel = 0;
                }
            }
            // Sağ-Üst Köşe
            else if (cx >= width - radius && cy < radius) {
                int dx = cx - (width - radius - 1);
                int dy = radius - cy;
                if ((dx * dx + dy * dy) > (radius * radius)) {
                    draw_pixel = 0;
                }
            }
            // Sol-Alt Köşe
            else if (cx < radius && cy >= height - radius) {
                int dx = radius - cx;
                int dy = cy - (height - radius - 1);
                if ((dx * dx + dy * dy) > (radius * radius)) {
                    draw_pixel = 0;
                }
            }
            // Sağ-Alt Köşe
            else if (cx >= width - radius && cy >= height - radius) {
                int dx = cx - (width - radius - 1);
                int dy = cy - (height - radius - 1);
                if ((dx * dx + dy * dy) > (radius * radius)) {
                    draw_pixel = 0;
                }
            }

            if (draw_pixel) {
                gfx_draw_pixel(x + cx, y + cy, color);
            }
        }
    }
}