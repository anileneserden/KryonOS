#ifndef WM_H
#define WM_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_WINDOWS 10
#define MAX_PANELS 4
#define MAX_LABELS 4

typedef struct {
    int x, y;
    int width, height;
    uint32_t color;
} panel_t;

typedef struct {
    int x, y;
    uint32_t color;
    char text[64];
} label_item_t;

typedef struct {
    int x, y;
    int width, height;
    char title[32];
    bool is_active;
    bool is_dragging;
    int drag_offset_x;
    int drag_offset_y;

    // Çoklu Panel Desteği
    panel_t panels[MAX_PANELS];
    int panel_count;

    // Çoklu Renkli Label Desteği
    label_item_t labels[MAX_LABELS];
    int label_count;
} window_t;

void wm_init(void);
window_t* wm_create_window(int width, int height, const char* title);
window_t* wm_get_active_window(void);
void wm_draw_window(window_t* win);
void wm_draw_all(void);

window_t* wm_find_at(int x, int y);
void wm_process_input(void);

#endif