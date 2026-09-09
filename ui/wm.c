#include <ui/wm.h>
#include <kernel/drivers/video/fb.h>
#include <kernel/drivers/video/gfx.h>
#include <kernel/serial.h>

extern int32_t mouse_x;
extern int32_t mouse_y;
extern uint8_t mouse_buttons;

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

    // İlk oluşturulduğunda hemen çiz
    wm_draw_window(win);
    return win;
}

void wm_draw_window(window_t* win) {
    if (!win || win->width == 0) return;

    // 1. Pencere Gövdesi (Arka plan)
    gfx_fill_rect(win->x, win->y, win->width, win->height, 0xFF303030);

    // 2. Başlık Çubuğu
    uint32_t title_color = win->is_active ? 0xFF007ACC : 0xFF505050;
    gfx_fill_rect(win->x, win->y, win->width, 24, title_color);

    // 3. Başlık Metni
    gfx_draw_text_utf8(win->x + 8, win->y + 6, 0xFFFFFFFF, win->title);
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
            y >= win->y && y <= win->y + 24) {
            return win;
        }
    }
    return 0;
}

static void wm_bring_to_front(window_t* win) {
    if (!win || window_count <= 1) return;

    int idx = -1;
    for (int i = 0; i < window_count; i++) {
        if (&window_list[i] == win) {
            idx = i;
            break;
        }
    }

    if (idx == -1 || idx == window_count - 1) return;

    window_t temp = window_list[idx];
    for (int i = idx; i < window_count - 1; i++) {
        window_list[i] = window_list[i + 1];
    }
    window_list[window_count - 1] = temp;

    for (int i = 0; i < window_count; i++) {
        window_list[i].is_active = (&window_list[i] == win);
    }
    active_window = win;
    
    // Sıralama değiştiği için tüm pencereleri yeniden çiz
    fb_clear(0xFF0000FF);
    wm_draw_all();
}

void wm_process_input(void) {
    uint8_t left_pressed = mouse_buttons & 0x01;
    uint8_t prev_left = prev_buttons & 0x01;

    // Eğer pencere sürükleniyorsa
    if (dragged_window && dragged_window->is_dragging) {
        int new_x = mouse_x - dragged_window->drag_offset_x;
        int new_y = mouse_y - dragged_window->drag_offset_y;

        // Konum gerçekten değiştiyse kısmi temizlik ve çizim yap
        if (new_x != dragged_window->x || new_y != dragged_window->y) {
            
            // 1. ADIM: Pencerenin ESKİ yerini mavi arkaplanla temizle (arkada iz kalmaması için)
            gfx_fill_rect(dragged_window->x, dragged_window->y, dragged_window->width, dragged_window->height, 0xFF0000FF);

            // 2. ADIM: Koordinatları güncelle
            dragged_window->x = new_x;
            dragged_window->y = new_y;

            // 3. ADIM: Altta kalan diğer pencerelerin bu alanla kesişen kısımlarını tekrar çiz (Alt alta binişme sorununu çözer)
            for (int i = 0; i < window_count; i++) {
                if (&window_list[i] != dragged_window && window_list[i].width > 0) {
                    wm_draw_window(&window_list[i]);
                }
            }

            // 4. ADIM: Sürüklenen pencereyi YENİ konumuna çiz
            wm_draw_window(dragged_window);
        }
    }

    if (left_pressed && !prev_left) {
        window_t* target = wm_find_at(mouse_x, mouse_y);
        if (target) {
            wm_bring_to_front(target);
            dragged_window = target;
            target->is_dragging = true;
            target->drag_offset_x = mouse_x - target->x;
            target->drag_offset_y = mouse_y - target->y;
        }
    } else if (!left_pressed && prev_left) {
        if (dragged_window) {
            dragged_window->is_dragging = false;
            dragged_window = 0;
        }
    }

    prev_buttons = mouse_buttons;
}