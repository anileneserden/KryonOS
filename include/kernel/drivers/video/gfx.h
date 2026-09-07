/* include/kernel/drivers/video/gfx.h */

#ifndef GFX_H
#define GFX_H

#include <stdint.h>

void gfx_draw_pixel(int x, int y, uint32_t color);
void gfx_fill_rect(int x, int y, int width, int height, uint32_t color);
void gfx_fill_rounded_rect(int x, int y, int width, int height, int radius, uint32_t color);

#endif