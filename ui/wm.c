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

static window_t window_list[MAX_WINDOWS];
static int window_count = 0;
static window_t* active_window = 0;
static window_t* dragged_window = 0;
static uint8_t prev_buttons = 0;

void wm_init(void) {
    window_count = 0;
    active_window = 0;
    dragged_window = 0;
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
    win->width = width;
    win->height = height;
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

// Pencereyi tamamen sistemden kaldıran fonksiyon
void wm_close_window(window_t* win) {
    if (!win) return;

    // Pencerenin kapladığı alanı hasarlı işaretle ki arkası yeniden çizilsin
    damage_union_rect(win->x, win->y, win->width, win->height);

    win->width = 0; // Kapatıldı olarak işaretle

    // Eğer kapanan aktif pencere ise başka bir pencereyi öne getir
    if (active_window == win) {
        active_window = 0;
        for (int i = window_count - 1; i >= 0; i--) {
            if (window_list[i].width > 0) {
                active_window = &window_list[i];
                active_window->is_active = true;
                break;
            }
        }
    }

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
    gfx_fill_rect(btn_x, btn_y, 18, 18, 0xFFE81123); // Kırmızı buton
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

static window_t* wm_bring_to_front(window_t* win) {
    if (!win || window_count <= 1) return win;

    int idx = -1;
    for (int i = 0; i < window_count; i++) {
        if (&window_list[i] == win) {
            idx = i;
            break;
        }
    }

    if (idx == -1 || idx == window_count - 1) return win;

    window_t temp = window_list[idx];
    for (int i = idx; i < window_count - 1; i++) {
        window_list[i] = window_list[i + 1];
    }
    window_list[window_count - 1] = temp;

    for (int i = 0; i < window_count; i++) {
        window_list[i].is_active = (&window_list[i] == &window_list[window_count - 1]);
    }
    active_window = &window_list[window_count - 1];
    
    damage_union_rect(0, 0, fb_get_width(), fb_get_height());
    return active_window;
}

void wm_process_input(void) {
    uint8_t left_pressed = mouse_buttons & 0x01;
    uint8_t prev_left = prev_buttons & 0x01;

    // 1. Sürükleme Mantığı
    if (dragged_window && dragged_window->is_dragging) {
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

            cursor_show();
        }
    }

    // 2. Tıklama Mantığı
    if (left_pressed && !prev_left) {
        window_t* target = wm_find_at(mouse_x, mouse_y);
        if (target) {
            cursor_prepare_redraw();
            
            // [X] Kapat butonuna basıldı mı kontrol et
            int btn_x = target->x + target->width - 22;
            int btn_y = target->y + 3;

            if (mouse_x >= btn_x && mouse_x <= btn_x + 18 &&
                mouse_y >= btn_y && mouse_y <= btn_y + 18) {
                wm_close_window(target);
                cursor_show();
                prev_buttons = mouse_buttons;
                return;
            }

            dragged_window = wm_bring_to_front(target);
            dragged_window->is_dragging =
                mouse_y >= dragged_window->y &&
                mouse_y < dragged_window->y + WM_TITLEBAR_HEIGHT;

            if (dragged_window->is_dragging) {
                dragged_window->drag_offset_x = mouse_x - dragged_window->x;
                dragged_window->drag_offset_y = mouse_y - dragged_window->y;
            }

            desktop_redraw();
            cursor_show();
        }
    }
    // 3. Bırakma Mantığı
    else if (!left_pressed && prev_left) {
        if (dragged_window) {
            dragged_window->is_dragging = false;
            dragged_window = 0;
        }
    }

    prev_buttons = mouse_buttons;
}