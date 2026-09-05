#ifndef KERNEL_DRIVERS_VIDEO_FB_H
#define KERNEL_DRIVERS_VIDEO_FB_H

#include <stdint.h>
#include <kernel/multiboot.h>

void fb_init(multiboot_info_t* mboot);
void fb_putpixel(uint32_t x, uint32_t y, uint32_t color);
uint32_t fb_getpixel(uint32_t x, uint32_t y);
void fb_clear(uint32_t color);
void fb_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
uint32_t fb_get_width(void);
uint32_t fb_get_height(void);

#endif