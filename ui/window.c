#include <ui/window.h>
#include <kernel/drivers/video/gfx.h>

void window_draw(window_t* win) {
    if (!win) return;

    int radius = 8; // Window corner rounding radius

    // 1. Window body (rounded background)
    gfx_fill_rounded_rect(win->x, win->y, win->width, win->height, radius, 0xFFE0E0E0);

    // 2. Title bar (area with rounded top corners and square bottom corners, or the entire top section)
    uint32_t title_bar_color = win->is_active ? 0xFF005A9E : 0xFF606060;
    gfx_fill_rect(win->x, win->y, win->width, WINDOW_TITLE_HEIGHT, title_bar_color);

    // 3. Close button (small red square in the top-right corner of the title bar)
    uint32_t btn_size = 14;
    uint32_t btn_x = win->x + win->width - btn_size - 6;
    uint32_t btn_y = win->y + (WINDOW_TITLE_HEIGHT - btn_size) / 2;
    gfx_fill_rect(btn_x, btn_y, btn_size, btn_size, 0xFFE81123);
}