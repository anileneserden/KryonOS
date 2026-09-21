#include <ui/wm.h>
#include <ui/desktop.h>
#include <kernel/drivers/video/fb.h>
#include <kernel/drivers/video/gfx.h>
#include <kernel/serial.h>
#include <ui/cursor.h>

extern int32_t mouse_x;
extern int32_t mouse_y;
extern uint8_t mouse_buttons;

#define WM_TITLEBAR_HEIGHT 24
#define RESIZE_BORDER 6
#define MIN_WINDOW_WIDTH 120
#define MIN_WINDOW_HEIGHT 80

typedef enum {
    RESIZE_NONE = 0,
    RESIZE_TOP = 1,
    RESIZE_BOTTOM = 2,
    RESIZE_LEFT = 4,
    RESIZE_RIGHT = 8,
    RESIZE_TOP_LEFT = RESIZE_TOP | RESIZE_LEFT,
    RESIZE_TOP_RIGHT = RESIZE_TOP | RESIZE_RIGHT,
    RESIZE_BOTTOM_LEFT = RESIZE_BOTTOM | RESIZE_LEFT,
    RESIZE_BOTTOM_RIGHT = RESIZE_BOTTOM | RESIZE_RIGHT
} resize_dir_t;

static window_t window_list[MAX_WINDOWS];
static int window_count = 0;
static window_t* active_window = 0;
static window_t* dragged_window = 0;
static window_t* resized_window = 0;
static window_t* hovered_window = 0;
static uint8_t hovered_close_button = 0;
static int resize_direction = RESIZE_NONE;
static uint8_t prev_buttons = 0;

void wm_init(void) {
    window_count = 0;
    active_window = 0;
    dragged_window = 0;
    resized_window = 0;
    hovered_window = 0;
    hovered_close_button = 0;
    resize_direction = RESIZE_NONE;
    prev_buttons = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        window_list[i].is_active = false;
        window_list[i].is_dragging = false;
        window_list[i].width = 0;
    }
}

window_t* wm_create_window(int width, int height, const char* title) {
    if (window_count >= MAX_WINDOWS) return 0;

    window_t* win = &window_list[window_count];
    win->x = 60 + (window_count * 30);
    win->y = 60 + (window_count * 30);
    win->width = width < MIN_WINDOW_WIDTH ? MIN_WINDOW_WIDTH : width;
    win->height = height < MIN_WINDOW_HEIGHT ? MIN_WINDOW_HEIGHT : height;
    win->is_active = true;
    win->is_dragging = false;
    win->drag_offset_x = 0;
    win->drag_offset_y = 0;

    int i = 0;
    while (title[i] != '\0' && i < 31) {
        win->title[i] = title[i];
        i++;
    }
    win->title[i] = '\0';

    for (int j = 0; j < window_count; j++) {
        window_list[j].is_active = false;
    }

    win->is_active = true;
    active_window = win;
    window_count++;

    damage_union_rect(0, 0, fb_get_width(), fb_get_height());
    desktop_redraw();
    return win;
}

void wm_close_window(window_t* win) {
    if (!win) return;

    win->width = 0;

    if (active_window == win) {
        active_window = 0;
    }

    for (int i = 0; i < window_count; i++) {
        if (window_list[i].width > 0) {
            window_list[i].is_active = (&window_list[i] == active_window);
        } else {
            window_list[i].is_active = false;
        }
    }

    damage_union_rect(0, 0, fb_get_width(), fb_get_height());
    desktop_redraw();
}

void wm_draw_window(window_t* win) {
    if (!win || win->width == 0) return;

    bool title_hovered = hovered_window == win;
    bool close_hovered = title_hovered && hovered_close_button;

    // 1. Window body
    gfx_fill_rect(win->x, win->y, win->width, win->height, 0xFF303030);

    // 2. Title bar
    uint32_t title_color = title_hovered
        ? (win->is_active ? 0xFF1088D0 : 0xFF666666)
        : (win->is_active ? 0xFF007ACC : 0xFF505050);
    gfx_fill_rect(win->x, win->y, win->width, 24, title_color);

    // 3. Title text
    gfx_draw_text_utf8(win->x + 8, win->y + 6, 0xFFFFFFFF, win->title);

    // 4. Close [X] button (top-right corner)
    int btn_x = win->x + win->width - 22;
    int btn_y = win->y + 3;
    gfx_fill_rect(btn_x, btn_y, 18, 18, close_hovered ? 0xFFFF5A5F : 0xFFE81123);
    gfx_draw_text_utf8(btn_x + 5, btn_y + 2, 0xFFFFFFFF, "X");

    // 5. Pencere içi label (varsa pencereyle birlikte yeniden çizilir)
    if (win->has_label) {
        int abs_x = win->x + win->label_rel_x;
        int abs_y = win->y + 24 + win->label_rel_y; // 24 piksel başlık çubuğu payı
        gfx_draw_text_utf8(abs_x, abs_y, 0xFFFFFFFF, win->label_text);
    }
}

void wm_draw_all(void) {
    for (int i = 0; i < window_count; i++) {
        if (window_list[i].width > 0) {
            wm_draw_window(&window_list[i]);
        }
    }
}

window_t* wm_find_at(int x, int y) {
    for (int i = window_count - 1; i >= 0; i--) {
        window_t* win = &window_list[i];
        if (win->width > 0 &&
            x >= win->x && x <= win->x + win->width &&
            y >= win->y && y <= win->y + win->height) {
            return win;
        }
    }
    return 0;
}

static int get_resize_direction(window_t* win, int x, int y) {
    if (!win) return RESIZE_NONE;

    int dir = RESIZE_NONE;
    int left = win->x;
    int right = win->x + win->width;
    int top = win->y;
    int bottom = win->y + win->height;

    // Tolerance for edge and corner detection
    if (x >= left - RESIZE_BORDER && x <= right + RESIZE_BORDER &&
        y >= top - RESIZE_BORDER && y <= bottom + RESIZE_BORDER) {
        
        if (x < left + RESIZE_BORDER) dir |= RESIZE_LEFT;
        else if (x > right - RESIZE_BORDER) dir |= RESIZE_RIGHT;

        if (y < top + RESIZE_BORDER) dir |= RESIZE_TOP;
        else if (y > bottom - RESIZE_BORDER) dir |= RESIZE_BOTTOM;
    }

    return dir;
}

static window_t* wm_bring_to_front(window_t* win) {
    if (!win || window_count <= 1) return win;

    int idx = -1;
    for (int i = 0; i < window_count; i++) {
        if (&window_list[i] == win) {
            idx = i;
            break;
        }
    }

    if (idx == -1) return win;

    if (idx == window_count - 1) {
        win->is_active = true;
        active_window = win;
        return win;
    }

    window_t temp = window_list[idx];
    for (int i = idx; i < window_count - 1; i++) {
        window_list[i] = window_list[i + 1];
    }
    window_list[window_count - 1] = temp;

    for (int i = 0; i < window_count; i++) {
        if (window_list[i].width > 0) {
            window_list[i].is_active = (i == window_count - 1);
        }
    }
    
    active_window = &window_list[window_count - 1];
    
    damage_union_rect(0, 0, fb_get_width(), fb_get_height());
    return active_window;
}

static void wm_update_hover_state(void) {
    window_t* new_hovered_window = wm_find_at(mouse_x, mouse_y);
    uint8_t new_hovered_close = 0;

    if (new_hovered_window
        && mouse_y >= new_hovered_window->y
        && mouse_y < new_hovered_window->y + WM_TITLEBAR_HEIGHT) {
        int close_x = new_hovered_window->x + new_hovered_window->width - 22;
        int close_y = new_hovered_window->y + 3;
        new_hovered_close = mouse_x >= close_x && mouse_x <= close_x + 18
            && mouse_y >= close_y && mouse_y <= close_y + 18;
    } else {
        new_hovered_window = 0;
    }

    if (new_hovered_window == hovered_window
        && new_hovered_close == hovered_close_button) return;

    if (hovered_window) {
        damage_union_rect(hovered_window->x, hovered_window->y,
                          hovered_window->width, WM_TITLEBAR_HEIGHT);
    }
    if (new_hovered_window) {
        damage_union_rect(new_hovered_window->x, new_hovered_window->y,
                          new_hovered_window->width, WM_TITLEBAR_HEIGHT);
    }

    hovered_window = new_hovered_window;
    hovered_close_button = new_hovered_close;
}

void wm_process_input(void) {
    wm_update_hover_state();

    uint8_t left_pressed = mouse_buttons & 0x01;
    uint8_t prev_left = prev_buttons & 0x01;

    // 1. If the left button was released, dragging or resizing must end.
    if (!left_pressed) {
        if (dragged_window || resized_window) {
            cursor_prepare_redraw();
            if (dragged_window) {
                damage_union_rect(dragged_window->x, dragged_window->y, dragged_window->width, dragged_window->height);
                dragged_window->is_dragging = false;
                dragged_window = 0;
            }
            if (resized_window) {
                damage_union_rect(resized_window->x, resized_window->y, resized_window->width, resized_window->height);
                resized_window = 0;
                resize_direction = RESIZE_NONE;
            }
            desktop_redraw();
            cursor_sync_position();
            cursor_show();
        }
        prev_buttons = mouse_buttons;
        return; // No other window movement can be processed while the button is not pressed
    }

    // 2. If the left button is pressed and was just clicked (click/focus/drag start)
    if (left_pressed && !prev_left) {
        window_t* target = wm_find_at(mouse_x, mouse_y);
        if (target) {
            window_t* front_win = wm_bring_to_front(target);

            int btn_x = target->x + target->width - 22;
            int btn_y = target->y + 3;

            cursor_prepare_redraw();

            // [X] Close button
            if (mouse_x >= btn_x && mouse_x <= btn_x + 18 &&
                mouse_y >= btn_y && mouse_y <= btn_y + 18) {
                wm_close_window(target);
                cursor_sync_position();
                cursor_show();
                prev_buttons = mouse_buttons;
                return;
            }

            // Resize check
            int dir = get_resize_direction(target, mouse_x, mouse_y);
            if (dir != RESIZE_NONE) {
                resized_window = target;
                resize_direction = dir;
            } else {
                // Start dragging only when the title bar was clicked
                if (mouse_y >= target->y && mouse_y < target->y + WM_TITLEBAR_HEIGHT) {
                    dragged_window = front_win;
                    dragged_window->is_dragging = true;
                    dragged_window->drag_offset_x = mouse_x - target->x;
                    dragged_window->drag_offset_y = mouse_y - target->y;
                }
            }

            desktop_redraw();
            cursor_sync_position();
            cursor_show();
        } 
        else {
            // Remove focus when the desktop background is clicked
            if (active_window != 0) {
                cursor_prepare_redraw();
                
                // Mark all window areas as damaged so their title bars return to gray
                for (int i = 0; i < window_count; i++) {
                    if (window_list[i].width > 0) {
                        damage_union_rect(window_list[i].x, window_list[i].y, window_list[i].width, window_list[i].height);
                        window_list[i].is_active = false;
                    }
                }
                
                active_window = 0;

                // Include the cursor position in the damaged area
                int32_t cur_x, cur_y;
                cursor_get_position(&cur_x, &cur_y);
                damage_union_rect(cur_x, cur_y, CURSOR_WIDTH, CURSOR_HEIGHT);

                desktop_redraw();
                cursor_sync_position();
                cursor_show();
            }
        }
    }
    // 3. The left button is held and the mouse is moving (dragging or resizing continues)
    else if (left_pressed && prev_left) {
        if (resized_window && resize_direction != RESIZE_NONE) {
            int old_x = resized_window->x;
            int old_y = resized_window->y;
            int old_w = resized_window->width;
            int old_h = resized_window->height;

            int new_x = old_x;
            int new_y = old_y;
            int new_w = old_w;
            int new_h = old_h;

            if (resize_direction & RESIZE_LEFT) {
                int max_x = old_x + old_w - MIN_WINDOW_WIDTH;
                new_x = mouse_x;
                if (new_x > max_x) new_x = max_x;
                new_w = old_w + (old_x - new_x);
            } else if (resize_direction & RESIZE_RIGHT) {
                new_w = mouse_x - old_x;
                if (new_w < MIN_WINDOW_WIDTH) new_w = MIN_WINDOW_WIDTH;
            }

            if (resize_direction & RESIZE_TOP) {
                int max_y = old_y + old_h - MIN_WINDOW_HEIGHT;
                new_y = mouse_y;
                if (new_y > max_y) new_y = max_y;
                new_h = old_h + (old_y - new_y);
            } else if (resize_direction & RESIZE_BOTTOM) {
                new_h = mouse_y - old_y;
                if (new_h < MIN_WINDOW_HEIGHT) new_h = MIN_WINDOW_HEIGHT;
            }

            if (new_x != old_x || new_y != old_y || new_w != old_w || new_h != old_h) {
                int32_t old_cursor_x, old_cursor_y;
                cursor_get_position(&old_cursor_x, &old_cursor_y);

                cursor_prepare_redraw();

                resized_window->is_active = true;
                active_window = resized_window;

                damage_union_rect(old_x, old_y, old_w, old_h);
                resized_window->x = new_x;
                resized_window->y = new_y;
                resized_window->width = new_w;
                resized_window->height = new_h;
                damage_union_rect(new_x, new_y, new_w, new_h);

                damage_union_rect(old_cursor_x, old_cursor_y, CURSOR_WIDTH, CURSOR_HEIGHT);
                damage_union_rect(mouse_x, mouse_y, CURSOR_WIDTH, CURSOR_HEIGHT);

                desktop_redraw();

                cursor_sync_position();
                cursor_show();
            }
        }
        else if (dragged_window && dragged_window->is_dragging) {
            // Target window coordinates based on the cursor position
            int new_x = mouse_x - dragged_window->drag_offset_x;
            int new_y = mouse_y - dragged_window->drag_offset_y;

            // Get the screen boundaries
            int screen_w = fb_get_width();
            int screen_h = fb_get_height();

            // Constrain the window to follow the cursor while keeping part of the title bar
            // (for example, 50 pixels) visible and reachable on screen.
            
            // Left boundary: keep at least 50 pixels of the window's right side visible
            if (new_x + dragged_window->width < 50) {
                new_x = 50 - dragged_window->width;
            }
            // Right boundary: keep the window's left side from extending too far past the screen
            if (new_x > screen_w - 50) {
                new_x = screen_w - 50;
            }

            // Top boundary: keep the title bar from disappearing above the screen
            if (new_y < 0) {
                new_y = 0;
            }
            
            // Bottom boundary: keep the title bar visible above the taskbar
            int taskbar_h = 36;
            
            if (new_y > screen_h - taskbar_h - WM_TITLEBAR_HEIGHT) {
                new_y = screen_h - taskbar_h - WM_TITLEBAR_HEIGHT;
            }

            if (new_x != dragged_window->x || new_y != dragged_window->y) {
                int old_x = dragged_window->x;
                int old_y = dragged_window->y;
                int32_t old_cursor_x, old_cursor_y;

                cursor_get_position(&old_cursor_x, &old_cursor_y);
                
                cursor_prepare_redraw();

                dragged_window->is_active = true;
                active_window = dragged_window;

                dragged_window->x = new_x;
                dragged_window->y = new_y;

                damage_union_rect(old_x, old_y, dragged_window->width, dragged_window->height);
                damage_union_rect(new_x, new_y, dragged_window->width, dragged_window->height);
                
                damage_union_rect(old_cursor_x, old_cursor_y, CURSOR_WIDTH, CURSOR_HEIGHT);
                damage_union_rect(mouse_x, mouse_y, CURSOR_WIDTH, CURSOR_HEIGHT);
                
                desktop_redraw();

                cursor_sync_position();
                cursor_show();
            }
        }
        else {
            // IMPORTANT: The left button is held, but no window is being dragged.
            // The cursor is only passing over the windows.
            int32_t old_cursor_x, old_cursor_y;
            cursor_get_position(&old_cursor_x, &old_cursor_y);

            if (old_cursor_x != mouse_x || old_cursor_y != mouse_y) {
                cursor_prepare_redraw();

                // Mark the cursor's old and new positions as damaged so the underlying
                // title bar/window colors are redrawn correctly.
                damage_union_rect(old_cursor_x, old_cursor_y, CURSOR_WIDTH, CURSOR_HEIGHT);
                damage_union_rect(mouse_x, mouse_y, CURSOR_WIDTH, CURSOR_HEIGHT);

                desktop_redraw();

                cursor_sync_position();
                cursor_show();
            }
        }
    }

    prev_buttons = mouse_buttons;
}

window_t* wm_get_active_window(void) {
    return active_window;
}