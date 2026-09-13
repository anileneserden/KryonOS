#ifndef WM_H
#define WM_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_WINDOWS 10
#define MAX_WINDOW_TEXTS 16
#define MAX_WINDOW_RECTS 16

typedef struct {
    int x, y, width, height;
    uint32_t color;
} window_rect_t;

typedef struct {
    int x, y;
    uint32_t color;
    char text[64];
} window_text_t;

typedef struct {
    int x, y;
    int width, height;
    char title[32];
    bool is_active;
    bool is_dragging;
    int drag_offset_x;
    int drag_offset_y;
    
    // Pencere içerik tamponları (Sürükleme ve yeniden çizilmelerde kaybolmayı önler)
    window_text_t texts[MAX_WINDOW_TEXTS];
    int text_count;

    window_rect_t rects[MAX_WINDOW_RECTS];
    int rect_count;
} window_t;

void wm_init(void);
window_t* wm_create_window(int width, int height, const char* title);
window_t* wm_get_active_window(void);
void wm_add_window_text(window_t* win, int x, int y, const char* text, uint32_t color);
void wm_add_window_rect(window_t* win, int x, int y, int width, int height, uint32_t color);
void wm_draw_window(window_t* win);
void wm_draw_all(void);

window_t* wm_find_at(int x, int y);
void wm_process_input(void);

#endif