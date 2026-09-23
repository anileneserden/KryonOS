#ifndef WM_H
#define WM_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_WINDOWS  10
#define MAX_PANELS   16
#define MAX_LABELS   32
#define MAX_BUTTONS  24

typedef struct {
    int x, y;
    int width, height;
    uint32_t color;
    uint32_t hover_color;
    bool is_hovered;
    void (*on_click)(void);
    void (*on_hover)(void);
    uint8_t anchor;
    int init_x, init_y, init_width, init_height;
    int init_win_w, init_win_h;
} panel_t;

typedef struct {
    int x, y;
    uint32_t color;
    char text[64];
    uint8_t anchor;
    int init_x, init_y;
    int init_win_w, init_win_h;
} label_item_t;

typedef struct {
    int x, y;
    int width, height;
    uint32_t bg_color;
    uint32_t hover_color;
    uint32_t text_color;
    char text[32];
    bool is_hovered;
    void (*on_click)(void);
    uint8_t anchor;
    int init_x, init_y, init_width, init_height;
    int init_win_w, init_win_h;
} button_t;

typedef struct {
    int x, y;
    int width, height;
    
    int min_width;
    int min_height;

    char title[32];
    bool is_active;
    bool is_dragging;
    int drag_offset_x;
    int drag_offset_y;

    int init_win_w;
    int init_win_h;

    panel_t panels[MAX_PANELS];
    int panel_count;

    label_item_t labels[MAX_LABELS];
    int label_count;

    button_t buttons[MAX_BUTTONS];
    int button_count;
} window_t;

void wm_init(void);
window_t* wm_create_window(int width, int height, const char* title);
window_t* wm_get_active_window(void);
void wm_draw_window(window_t* win);
void wm_draw_all(void);

window_t* wm_find_at(int x, int y);
void wm_process_input(void);

#endif