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
static int resize_direction = RESIZE_NONE;
static uint8_t prev_buttons = 0;

void wm_init(void) {
    window_count = 0;
    active_window = 0;
    dragged_window = 0;
    resized_window = 0;
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

    // 1. Pencere Gövdesi
    gfx_fill_rect(win->x, win->y, win->width, win->height, 0xFF303030);

    // 2. Başlık Çubuğu
    uint32_t title_color = win->is_active ? 0xFF007ACC : 0xFF505050;
    gfx_fill_rect(win->x, win->y, win->width, 24, title_color);

    // 3. Başlık Metni
    gfx_draw_text_utf8(win->x + 8, win->y + 6, 0xFFFFFFFF, win->title);

    // 4. Kapat [X] Butonu (Sağ üst köşe)
    int btn_x = win->x + win->width - 22;
    int btn_y = win->y + 3;
    gfx_fill_rect(btn_x, btn_y, 18, 18, 0xFFE81123);
    gfx_draw_text_utf8(btn_x + 5, btn_y + 2, 0xFFFFFFFF, "X");
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

    // Kenar ve köşe kontrolü için tolerans payı
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

void wm_process_input(void) {
    uint8_t left_pressed = mouse_buttons & 0x01;
    uint8_t prev_left = prev_buttons & 0x01;

    // 1. Boyutlandırma (Resizing) Mantığı
    if (resized_window && resize_direction != RESIZE_NONE) {
        int old_x = resized_window->x;
        int old_y = resized_window->y;
        int old_w = resized_window->width;
        int old_h = resized_window->height;

        int new_x = old_x;
        int new_y = old_y;
        int new_w = old_w;
        int new_h = old_h;

        // 8 Yöne Göre Boyut ve Konum Hesaplama
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

            // Eski ve yeni alanları hasarlı işaretle
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
    // 2. Sürükleme (Dragging) Mantığı
    else if (dragged_window && dragged_window->is_dragging) {
        int new_x = mouse_x - dragged_window->drag_offset_x;
        int new_y = mouse_y - dragged_window->drag_offset_y;

        if (new_x != dragged_window->x || new_y != dragged_window->y) {
            int old_x = dragged_window->x;
            int old_y = dragged_window->y;
            int32_t old_cursor_x;
            int32_t old_cursor_y;

            cursor_get_position(&old_cursor_x, &old_cursor_y);
            
            cursor_prepare_redraw();

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

    // 3. Tıklama Mantığı (Focus, Kapatma, Resize veya Drag Başlatma)
    if (left_pressed && !prev_left) {
        window_t* target = wm_find_at(mouse_x, mouse_y);
        if (target) {
            // Aktif pencereyi öne getir
            dragged_window = wm_bring_to_front(target);

            // [X] Kapat butonuna basıldı mı kontrol et
            int btn_x = target->x + target->width - 22;
            int btn_y = target->y + 3;

            cursor_prepare_redraw();

            if (mouse_x >= btn_x && mouse_x <= btn_x + 18 &&
                mouse_y >= btn_y && mouse_y <= btn_y + 18) {
                wm_close_window(target);
                
                cursor_sync_position();
                cursor_show();
                
                prev_buttons = mouse_buttons;
                return;
            }

            // Köşe/Kenar Boyutlandırma (Resize) Kontrolü
            int dir = get_resize_direction(target, mouse_x, mouse_y);
            if (dir != RESIZE_NONE) {
                resized_window = target;
                resize_direction = dir;
            } else {
                // Başlık çubuğundaysa Sürükleme (Drag) Başlat
                dragged_window->is_dragging =
                    mouse_y >= dragged_window->y &&
                    mouse_y < dragged_window->y + WM_TITLEBAR_HEIGHT;

                if (dragged_window->is_dragging) {
                    dragged_window->drag_offset_x = mouse_x - dragged_window->x;
                    dragged_window->drag_offset_y = mouse_y - dragged_window->y;
                }
            }

            desktop_redraw();
            
            cursor_sync_position();
            cursor_show();
        } 
        else {
            // Masaüstüne tıklandı (Seçimi kaldır / Null yap)
            if (active_window != 0) {
                cursor_prepare_redraw();

                active_window = 0;
                for (int i = 0; i < window_count; i++) {
                    if (window_list[i].width > 0) {
                        window_list[i].is_active = false;
                    }
                }

                desktop_redraw();
                
                cursor_sync_position();
                cursor_show();
            }
        }
    }
    // 4. Bırakma Mantığı (Sürükleme veya Boyutlandırma Bitti)
    else if (!left_pressed && prev_left) {
        if (dragged_window || resized_window) {
            if (dragged_window) {
                dragged_window->is_dragging = false;
                dragged_window = 0;
            }
            if (resized_window) {
                resized_window = 0;
                resize_direction = RESIZE_NONE;
            }
            
            cursor_prepare_redraw();
            cursor_sync_position();
            cursor_show();
        }
    }

    prev_buttons = mouse_buttons;
}