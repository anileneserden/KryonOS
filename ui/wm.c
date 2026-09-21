#include <ui/wm.h>
#include <ui/desktop.h>
#include <kernel/drivers/video/fb.h>
#include <kernel/drivers/video/gfx.h>
#include <kernel/serial.h>
#include <ui/cursor.h>
#include <kernel/kef.h>

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
static int resize_old_x = 0;
static int resize_old_y = 0;
static int resize_old_w = 0;
static int resize_old_h = 0;

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
    win->is_active = true;
    win->is_dragging = false;
    win->drag_offset_x = 0;
    win->drag_offset_y = 0;

    // Başlık uzunluğuna göre minimum genişlik hesapla ([X] butonu ve boşluklar dahil)
    int title_len = 0;
    while (title[title_len] != '\0' && title_len < 31) {
        win->title[title_len] = title[title_len];
        title_len++;
    }
    win->title[title_len] = '\0';

    // Başlık uzunluğuna göre hesapla ama içeriklerin (dosya listesi vb.) bozulmaması için 
    // en az 340 piksel (veya projen için gereken güvenli genişlik) taban sınır koyalım.
    int calculated_min_w = (title_len * 8) + 50;
    int global_min_w = 340; // İçeriklerin daralıp üst üste binmesini engelleyen güvenli genişlik
    
    if (calculated_min_w < global_min_w) {
        calculated_min_w = global_min_w;
    }

    win->min_width = calculated_min_w;
    win->min_height = 240; // Panellerin ve alt durum çubuğunun düzgün sığacağı minimum yükseklik

    win->width = width < win->min_width ? win->min_width : width;
    win->height = height < win->min_height ? win->min_height : height;

    // Pencere ilk oluşturulduğunda referans boyutlarını atayalım
    win->init_win_w = win->width;
    win->init_win_h = win->height;

    for (int j = 0; j < window_count; j++) {
        window_list[j].is_active = false;
    }

    win->is_active = true;
    active_window = win;
    window_count++;

    for (int l = 0; l < win->label_count; l++) {
        win->labels[l].init_x = win->labels[l].x;
        win->labels[l].init_y = win->labels[l].y;
        win->labels[l].init_win_w = win->width;
        win->labels[l].init_win_h = win->height;
    }

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

    // 2. Panelleri çiz (Gelişmiş kırpma destekli)
    for (int i = 0; i < win->panel_count; i++) {
        panel_t* p = &win->panels[i];
        
        int rel_x = p->x;
        int rel_y = 24 + p->y;
        int w = p->width;
        int h = p->height;

        // Sol veya üst sınırdan taşma kırpması
        if (rel_x < 0) { w += rel_x; rel_x = 0; }
        if (rel_y < 24) { h += (rel_y - 24); rel_y = 24; }

        // Sağ veya alt sınırdan taşma kırpması
        if (rel_x < win->width && rel_y < win->height) {
            if (rel_x + w > win->width) w = win->width - rel_x;
            if (rel_y + h > win->height) h = win->height - rel_y;

            if (w > 0 && h > 0) {
                gfx_fill_rect(win->x + rel_x, win->y + rel_y, w, h, p->color);
            }
        }
    }

    // 3. Title bar
    uint32_t title_color = title_hovered
        ? (win->is_active ? 0xFF1088D0 : 0xFF666666)
        : (win->is_active ? 0xFF007ACC : 0xFF505050);
    gfx_fill_rect(win->x, win->y, win->width, 24, title_color);

    // 4. Title text (Başlık taşıyorsa kırp veya sınırla)
    // Başlık çubuğu için basitçe x koordinatının pencere içinde kalması sağlanır
    if (win->width > 16) {
        // İsteğe bağlı: Başlık uzunluğu pencereyi aşacaksa buraya da kırpma eklenebilir
        gfx_draw_text_utf8(win->x + 8, win->y + 6, 0xFFFFFFFF, win->title);
    }

    // 5. Close [X] button (top-right corner)
    int btn_x = win->x + win->width - 22;
    int btn_y = win->y + 3;
    // Sadece pencere yeterince genişse kapatma butonunu göster
    if (win->width >= 30) {
        gfx_fill_rect(btn_x, btn_y, 18, 18, close_hovered ? 0xFFFF5A5F : 0xFFE81123);
        gfx_draw_text_utf8(btn_x + 5, btn_y + 2, 0xFFFFFFFF, "X");
    }

    // 6. Label'ları çiz (Pencere sınırları dışına çıkanları gizle - Metin taşıma korumasıyla)
    for (int i = 0; i < win->label_count; i++) {
        label_item_t* l = &win->labels[i];
        int rel_x = l->x;
        int rel_y = 24 + l->y; // 24 piksel başlık çubuğu payı

        // Etiket başlangıcı pencere sınırları içindeyse ve başlık çubuğunun altındaysa
        if (rel_x >= 0 && rel_x < win->width && rel_y >= 24 && rel_y < win->height) {
            // Not: Metnin sağa taşmasını engellemek için rel_x, pencere genişliğinden küçük olmalı
            // Eğer sisteminde metin uzunluğunu veren bir fonksiyon (örn. get_text_width) varsa buraya eklenebilir.
            // Şimdilik başlangıç noktası pencere içinde olanları güvenle çiziyoruz:
            gfx_draw_text_utf8(win->x + rel_x, win->y + rel_y, l->color, l->text);
        }
    }

    // 7. Butonları çiz (Gelişmiş kırpma destekli)
    for (int i = 0; i < win->button_count; i++) {
        button_t* b = &win->buttons[i];
        int rel_x = b->x;
        int rel_y = 24 + b->y;
        int bw = b->width;
        int bh = b->height;

        // Sol/Üst kırpma
        if (rel_x < 0) { bw += rel_x; rel_x = 0; }
        if (rel_y < 24) { bh += (rel_y - 24); rel_y = 24; }

        // Sağ/Alt kırpma
        if (rel_x < win->width && rel_y < win->height) {
            if (rel_x + bw > win->width) bw = win->width - rel_x;
            if (rel_y + bh > win->height) bh = win->height - rel_y;

            if (bw > 0 && bh > 0) {
                int abs_x = win->x + rel_x;
                int abs_y = win->y + rel_y;
                
                uint32_t current_bg_color = b->is_hovered ? (b->hover_color != 0 ? b->hover_color : 0xFF505050) : b->bg_color;
                
                // Buton arka planı (kırpılmış boyutlarla)
                gfx_fill_rect(abs_x, abs_y, bw, bh, current_bg_color);
                
                // Buton yazısı (Yalnızca butonun sol kısmı görünüyorsa çiz)
                if (bw > 10) {
                    gfx_draw_text_utf8(abs_x + 8, abs_y + 8, b->text_color, b->text);
                }
            }
        }
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
    for (int i = 0; i < window_count; i++) {
        window_t* win = &window_list[i];
        if (!win || win->width == 0) continue;

        for (int b = 0; b < win->button_count; b++) {
            button_t* btn = &win->buttons[b];
            
            // Pencere sol-üst köşesine 24 piksel başlık çubuğu payını net ekleyelim
            int abs_x = win->x + btn->x;
            int abs_y = win->y + 24 + btn->y;

            // Sınır kontrolü (Genişlik ve yükseklik taşmalarını önlemek için kesin sınır)
            if (mouse_x >= abs_x && mouse_x < abs_x + btn->width &&
                mouse_y >= abs_y && mouse_y < abs_y + btn->height) {
                
                // Eğer hover durumu değiştiyse ekranı o bölgede tazele
                if (!btn->is_hovered) {
                    btn->is_hovered = true;
                    damage_union_rect(abs_x, abs_y, btn->width, btn->height);
                }
            } else {
                if (btn->is_hovered) {
                    btn->is_hovered = false;
                    damage_union_rect(abs_x, abs_y, btn->width, btn->height);
                }
            }
        }
    }
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
                // BOYUTLANDIRMA BİTTİ: Referans (init) değerlerini yeni boyuta sabitle!
                // Böylece sonraki resize işleminde matematik ve sağa sabitleme (anchor) şaşmaz.
                resized_window->init_win_w = resized_window->width;
                resized_window->init_win_h = resized_window->height;

                for (int i = 0; i < resized_window->panel_count; i++) {
                    resized_window->panels[i].init_x = resized_window->panels[i].x;
                    resized_window->panels[i].init_y = resized_window->panels[i].y;
                    resized_window->panels[i].init_width = resized_window->panels[i].width;
                    resized_window->panels[i].init_height = resized_window->panels[i].height;
                    resized_window->panels[i].init_win_w = resized_window->width;
                    resized_window->panels[i].init_win_h = resized_window->height;
                }
                for (int i = 0; i < resized_window->button_count; i++) {
                    resized_window->buttons[i].init_x = resized_window->buttons[i].x;
                    resized_window->buttons[i].init_y = resized_window->buttons[i].y;
                    resized_window->buttons[i].init_width = resized_window->buttons[i].width;
                    resized_window->buttons[i].init_height = resized_window->buttons[i].height;
                    resized_window->buttons[i].init_win_w = resized_window->width;
                    resized_window->buttons[i].init_win_h = resized_window->height;
                }
                // ETİKETLERİN referans değerleri de bitiş anında güncelleniyor:
                for (int i = 0; i < resized_window->label_count; i++) {
                    resized_window->labels[i].init_x = resized_window->labels[i].x;
                    resized_window->labels[i].init_y = resized_window->labels[i].y;
                    resized_window->labels[i].init_win_w = resized_window->width;
                    resized_window->labels[i].init_win_h = resized_window->height;
                }

                // --- ÇÖZÜM: Pencerenin küçülmeden önceki ESKİ ve BÜYÜK alanını temizle ---
                damage_union_rect(resize_old_x, resize_old_y, resize_old_w, resize_old_h);
                // Pencerenin YENİ alanını da kirli işaretle
                damage_union_rect(resized_window->x, resized_window->y, resized_window->width, resized_window->height);
                
                resized_window = 0;
                resize_direction = RESIZE_NONE;
            }
            desktop_redraw();
            cursor_sync_position();
            cursor_show();
        }
        prev_buttons = mouse_buttons;
        return;
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

            // --- PENCERE İÇİ BUTON TIKLAMA KONTROLÜ ---
            bool clicked_on_button = false;
            for (int i = 0; i < target->button_count; i++) {
                button_t* b = &target->buttons[i]; 
                int abs_bx = target->x + b->x;
                int abs_by = target->y + 24 + b->y;

                if (mouse_x >= abs_bx && mouse_x < abs_bx + b->width &&
                    mouse_y >= abs_by && mouse_y < abs_by + b->height) {
                    
                    serial_write("WM: Pencere ici butona tiklandi: ");
                    serial_write(b->text);
                    serial_write("\n");

                    if (b->on_click != 0) {
                        b->on_click();
                    }

                    clicked_on_button = true;
                    break;
                }
            }

            // Resize check
            int dir = get_resize_direction(target, mouse_x, mouse_y);
            if (dir != RESIZE_NONE) {
                resized_window = target;
                resize_direction = dir;
                
                // BOYUTLANDIRMA BAŞLADI: O anki eski/büyük boyutları saklayalım
                resize_old_x = target->x;
                resize_old_y = target->y;
                resize_old_w = target->width;
                resize_old_h = target->height;
            } else if (!clicked_on_button) {
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
            if (active_window != 0) {
                cursor_prepare_redraw();
                
                for (int i = 0; i < window_count; i++) {
                    if (window_list[i].width > 0) {
                        damage_union_rect(window_list[i].x, window_list[i].y, window_list[i].width, window_list[i].height);
                        window_list[i].is_active = false;
                    }
                }
                
                active_window = 0;

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

            // Pencereye özel min sınırları kullanalım
            int min_w = resized_window->min_width > 0 ? resized_window->min_width : MIN_WINDOW_WIDTH;
            int min_h = resized_window->min_height > 0 ? resized_window->min_height : MIN_WINDOW_HEIGHT;

            if (resize_direction & RESIZE_LEFT) {
                int max_x = old_x + old_w - min_w;
                new_x = mouse_x;
                if (new_x > max_x) new_x = max_x;
                new_w = old_w + (old_x - new_x);
                if (new_w < min_w) {
                    new_w = min_w;
                    new_x = old_x + old_w - min_w;
                }
            } else if (resize_direction & RESIZE_RIGHT) {
                new_w = mouse_x - old_x;
                if (new_w < min_w) {
                    new_w = min_w;
                }
            }

            if (resize_direction & RESIZE_TOP) {
                int max_y = old_y + old_h - min_h;
                new_y = mouse_y;
                if (new_y > max_y) new_y = max_y;
                new_h = old_h + (old_y - new_y);
            } else if (resize_direction & RESIZE_BOTTOM) {
                new_h = mouse_y - old_y;
                if (new_h < min_h) new_h = min_h;
            }

            if (new_x != old_x || new_y != old_y || new_w != old_w || new_h != old_h) {
                int32_t old_cursor_x, old_cursor_y;
                cursor_get_position(&old_cursor_x, &old_cursor_y);

                cursor_prepare_redraw();

                resized_window->is_active = true;
                active_window = resized_window;

                // --- ÇÖZÜM: Boyutlandırma esnasında da hem eski alanı hem yeni alanı temizle ---
                damage_union_rect(resize_old_x, resize_old_y, resize_old_w, resize_old_h);
                
                resized_window->x = new_x;
                resized_window->y = new_y;
                resized_window->width = new_w;
                resized_window->height = new_h;

                // --- 1. PANELLER İÇİN ANCHOR HESAPLAMASI ---
                for (int i = 0; i < resized_window->panel_count; i++) {
                    panel_t* p = &resized_window->panels[i];
                    
                    if ((p->anchor & ANCHOR_LEFT) && (p->anchor & ANCHOR_RIGHT)) {
                        p->width = p->init_width + (resized_window->width - p->init_win_w);
                    } else if (p->anchor & ANCHOR_RIGHT) {
                        int right_margin = p->init_win_w - (p->init_x + p->init_width);
                        p->x = resized_window->width - right_margin - p->width;
                    }

                    if ((p->anchor & ANCHOR_TOP) && (p->anchor & ANCHOR_BOTTOM)) {
                        p->height = p->init_height + (resized_window->height - p->init_win_h);
                    } else if (p->anchor & ANCHOR_BOTTOM) {
                        int bottom_margin = p->init_win_h - (p->init_y + p->init_height);
                        p->y = resized_window->height - bottom_margin - p->height;
                    }
                }

                // --- 2. BUTONLAR İÇİN ANCHOR HESAPLAMASI ---
                for (int i = 0; i < resized_window->button_count; i++) {
                    button_t* b = &resized_window->buttons[i];

                    if ((b->anchor & ANCHOR_LEFT) && (b->anchor & ANCHOR_RIGHT)) {
                        b->width = b->init_width + (resized_window->width - b->init_win_w);
                    } else if (b->anchor & ANCHOR_RIGHT) {
                        int right_margin = b->init_win_w - (b->init_x + b->init_width);
                        b->x = resized_window->width - right_margin - b->width;
                    }

                    if ((b->anchor & ANCHOR_TOP) && (b->anchor & ANCHOR_BOTTOM)) {
                        b->height = b->init_height + (resized_window->height - b->init_win_h);
                    } else if (b->anchor & ANCHOR_BOTTOM) {
                        int bottom_margin = b->init_win_h - (b->init_y + b->init_height);
                        b->y = resized_window->height - bottom_margin - b->height;
                    }
                }

                // --- 3. ETİKETLER (LABELS) İÇİN ANCHOR HESAPLAMASI ---
                for (int i = 0; i < resized_window->label_count; i++) {
                    label_item_t* l = &resized_window->labels[i];

                    if (l->init_win_w == 0) l->init_win_w = resized_window->init_win_w;
                    if (l->init_win_h == 0) l->init_win_h = resized_window->init_win_h;

                    if (l->anchor & ANCHOR_RIGHT) {
                        int right_margin = l->init_win_w - l->init_x;
                        l->x = resized_window->width - right_margin;
                    }

                    if (l->anchor & ANCHOR_BOTTOM) {
                        int bottom_margin = l->init_win_h - l->init_y;
                        l->y = resized_window->height - bottom_margin;
                    }
                }

                damage_union_rect(new_x, new_y, new_w, new_h);

                damage_union_rect(old_cursor_x, old_cursor_y, CURSOR_WIDTH, CURSOR_HEIGHT);
                damage_union_rect(mouse_x, mouse_y, CURSOR_WIDTH, CURSOR_HEIGHT);

                desktop_redraw();

                cursor_sync_position();
                cursor_show();
            }
        }
        else if (dragged_window && dragged_window->is_dragging) {
            int new_x = mouse_x - dragged_window->drag_offset_x;
            int new_y = mouse_y - dragged_window->drag_offset_y;

            int screen_w = fb_get_width();
            int screen_h = fb_get_height();

            if (new_x + dragged_window->width < 50) {
                new_x = 50 - dragged_window->width;
            }
            if (new_x > screen_w - 50) {
                new_x = screen_w - 50;
            }
            if (new_y < 0) {
                new_y = 0;
            }
            
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
            int32_t old_cursor_x, old_cursor_y;
            cursor_get_position(&old_cursor_x, &old_cursor_y);

            if (old_cursor_x != mouse_x || old_cursor_y != mouse_y) {
                cursor_prepare_redraw();

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