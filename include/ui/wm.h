#ifndef WM_H
#define WM_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_WINDOWS 10

typedef struct {
    int x, y;
    int width, height;
    char title[32];
    bool is_active;
    bool is_dragging;
    int drag_offset_x;
    int drag_offset_y;
} window_t;

void wm_init(void);
window_t* wm_create_window(int width, int height, const char* title);
void wm_draw_window(window_t* win);
void wm_draw_all(void);

window_t* wm_find_at(int x, int y);
void wm_process_input(void);

#endif