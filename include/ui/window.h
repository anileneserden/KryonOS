#ifndef UI_WINDOW_H
#define UI_WINDOW_H

#include <stdint.h>
#include <stdbool.h>

#define WINDOW_TITLE_HEIGHT 24

typedef struct {
    int32_t x, y;
    uint32_t width, height;
    char title[32];
    bool is_active;
    bool is_dragging;
} window_t;

void window_draw(window_t* win);

#endif