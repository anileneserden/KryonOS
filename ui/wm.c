#include <ui/wm.h>
#include <ui/desktop.h>
#include <kernel/drivers/video/fb.h>
#include <kernel/drivers/video/gfx.h>
#include <kernel/serial.h>
#include <ui/cursor.h>
#include <kernel/loader/kef.h>
#include <stddef.h>

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
static text_input_t* active_input = 0;
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
    active_input = 0;
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

    // Calculate minimum width based on title length (including [X] button and padding)
    int title_len = 0;
    while (title[title_len] != '\0' && title_len < 31) {
        win->title[title_len] = title[title_len];
        title_len++;
    }
    win->title[title_len] = '\0';

    int calculated_min_w = (title_len * 8) + 50;
    int global_min_w = 340; 
    
    if (calculated_min_w < global_min_w) {
        calculated_min_w = global_min_w;
    }

    win->min_width = calculated_min_w;
    win->min_height = 240;

    win->width = width < win->min_width ? win->min_width : width;
    win->height = height < win->min_height ? win->min_height : height;

    // Assign reference dimensions when the window is first created
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

    // Combobox başlangıç (init) referans değerlerini kaydet
    for (int c = 0; c < win->combobox_count; c++) {
        win->comboboxes[c].init_x = win->comboboxes[c].x;
        win->comboboxes[c].init_y = win->comboboxes[c].y;
        win->comboboxes[c].init_width = win->comboboxes[c].width;
        win->comboboxes[c].init_height = win->comboboxes[c].height;
        win->comboboxes[c].init_win_w = win->width;
        win->comboboxes[c].init_win_h = win->height;
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
    gfx_fill_rect(win->x, win->y, win->width, win->height, win->bg_color);

    // 2. Draw panels (with advanced clipping support)
    for (int i = 0; i < win->panel_count; i++) {
        panel_t* p = &win->panels[i];
        
        int rel_x = p->x;
        int rel_y = 24 + p->y;
        int w = p->width;
        int h = p->height;

        // Left or top edge overflow clipping
        if (rel_x < 0) { w += rel_x; rel_x = 0; }
        if (rel_y < 24) { h += (rel_y - 24); rel_y = 24; }

        // Right or bottom edge overflow clipping
        if (rel_x < win->width && rel_y < win->height) {
            if (rel_x + w > win->width) w = win->width - rel_x;
            if (rel_y + h > win->height) h = win->height - rel_y;

            if (w > 0 && h > 0) {
                uint32_t current_color = p->is_hovered ? p->hover_color : p->color;
                gfx_fill_rect(win->x + rel_x, win->y + rel_y, w, h, current_color);
            }
        }
    }

    // 3. Title bar
    uint32_t title_color = title_hovered
        ? (win->is_active ? 0xFF1088D0 : 0xFF666666)
        : (win->is_active ? 0xFF007ACC : 0xFF505050);
    gfx_fill_rect(win->x, win->y, win->width, 24, title_color);

    // 4. Title text (clip or limit if title overflows)
    if (win->width > 16) {
        gfx_draw_text_utf8(win->x + 8, win->y + 6, 0xFFFFFFFF, win->title);
    }

    // 5. Close [X] button (top-right corner)
    int btn_x = win->x + win->width - 22;
    int btn_y = win->y + 3;
    // Show close button only if the window is wide enough
    if (win->width >= 30) {
        gfx_fill_rect(btn_x, btn_y, 18, 18, close_hovered ? 0xFFFF5A5F : 0xFFE81123);
        gfx_draw_text_utf8(btn_x + 5, btn_y + 2, 0xFFFFFFFF, "X");
    }

    // 6. Draw labels (hide those extending outside window boundaries - with text overflow protection)
    for (int i = 0; i < win->label_count; i++) {
        label_item_t* l = &win->labels[i];
        int rel_x = l->x;
        int rel_y = 24 + l->y; // 24 pixel titlebar offset

        if (rel_x >= 0 && rel_x < win->width && rel_y >= 24 && rel_y < win->height) {
            gfx_draw_text_utf8(win->x + rel_x, win->y + rel_y, l->color, l->text);
        }
    }

    // 7. Draw buttons (with advanced clipping support)
    for (int i = 0; i < win->button_count; i++) {
        button_t* b = &win->buttons[i];
        int rel_x = b->x;
        int rel_y = 24 + b->y;
        int bw = b->width;
        int bh = b->height;

        // Left/Top clipping
        if (rel_x < 0) { bw += rel_x; rel_x = 0; }
        if (rel_y < 24) { bh += (rel_y - 24); rel_y = 24; }

        // Right/Bottom clipping
        if (rel_x < win->width && rel_y < win->height) {
            if (rel_x + bw > win->width) bw = win->width - rel_x;
            if (rel_y + bh > win->height) bh = win->height - rel_y;

            if (bw > 0 && bh > 0) {
                int abs_x = win->x + rel_x;
                int abs_y = win->y + rel_y;
                
                uint32_t current_bg_color = b->is_hovered ? (b->hover_color != 0 ? b->hover_color : 0xFF505050) : b->bg_color;
                
                // Button background (with clipped dimensions)
                gfx_fill_rect(abs_x, abs_y, bw, bh, current_bg_color);
                
                // Button text (draw only if the left part of the button is visible)
                if (bw > 10) {
                    gfx_draw_text_utf8(abs_x + 8, abs_y + 8, b->text_color, b->text);
                }
            }
        }

        if (win->has_canvas && win->canvas_buffer) {
            int rel_x = win->canvas_x;
            int rel_y = 24 + win->canvas_y;
            int cw = win->canvas_w;
            int ch = win->canvas_h;

            if (rel_x < 0) { rel_x = 0; }
            if (rel_y < 24) { rel_y = 24; }

            if (rel_x < win->width && rel_y < win->height) {
                if (rel_x + cw > win->width) cw = win->width - rel_x;
                if (rel_y + ch > win->height) ch = win->height - rel_y;

                if (cw > 0 && ch > 0) {
                    gfx_draw_buffer(win->x + rel_x, win->y + rel_y, cw, ch, win->canvas_buffer, win->canvas_w);
                }
            }
        }
    }

    // [YENİ / DÜZELTİLMİŞ] 7.5. Draw Canvas (3D Oyun / Piksel Tamponu)
    if (win->has_canvas && win->canvas_buffer) {
        int rel_x = win->canvas_x;
        int rel_y = 24 + win->canvas_y; // 24 piksel başlık çubuğu payı
        int cw = win->canvas_w;
        int ch = win->canvas_h;

        // Sol/Üst kırpma (clipping)
        if (rel_x < 0) { cw += rel_x; rel_x = 0; }
        if (rel_y < 24) { ch += (rel_y - 24); rel_y = 24; }

        // Sağ/Alt kırpma
        if (rel_x < win->width && rel_y < win->height) {
            if (rel_x + cw > win->width) cw = win->width - rel_x;
            if (rel_y + ch > win->height) ch = win->height - rel_y;

            if (cw > 0 && ch > 0) {
                int abs_x = win->x + rel_x;
                int abs_y = win->y + rel_y;

                // Ham piksel buffer'ını ekrana / grafik alt sistemine aktar
                gfx_draw_buffer(abs_x, abs_y, cw, ch, win->canvas_buffer, win->canvas_w);
            }
        }
    }

    // 8. Draw pictureboxes (Resim Kutuları)
    for (int i = 0; i < win->picturebox_count; i++) {
        picturebox_t* p = &win->pictureboxes[i];
        int rel_x = p->x;
        int rel_y = 24 + p->y;
        int pw = p->width;
        int ph = p->height;

        // Kırpma (Clipping) kontrolleri
        if (rel_x < 0) { pw += rel_x; rel_x = 0; }
        if (rel_y < 24) { ph += (rel_y - 24); rel_y = 24; }

        if (rel_x < win->width && rel_y < win->height) {
            if (rel_x + pw > win->width) pw = win->width - rel_x;
            if (rel_y + ph > win->height) ph = win->height - rel_y;

            if (pw > 0 && ph > 0 && p->pixels) {
                int abs_x = win->x + rel_x;
                int abs_y = win->y + rel_y;

                // Eğer resim verisi yüklendiyse framebuffer/grafik arabelleğine çiz
                gfx_draw_buffer(abs_x, abs_y, pw, ph, p->pixels, p->img_width);
            }
        }
    }

    // 9. Draw text inputs (Metin Kutuları)
    for (int i = 0; i < win->input_count; i++) {
        text_input_t* inp = &win->inputs[i];
        int rel_x = inp->x;
        int rel_y = 24 + inp->y;
        int iw = inp->width;
        int ih = inp->height;

        // Kırpma (Clipping) kontrolleri
        if (rel_x < 0) { iw += rel_x; rel_x = 0; }
        if (rel_y < 24) { ih += (rel_y - 24); rel_y = 24; }

        if (rel_x < win->width && rel_y < win->height) {
            if (rel_x + iw > win->width) iw = win->width - rel_x;
            if (rel_y + ih > win->height) ih = win->height - rel_y;

            if (iw > 0 && ih > 0) {
                int abs_x = win->x + rel_x;
                int abs_y = win->y + rel_y;

                // Kutunun arkaplanını çiz
                gfx_fill_rect(abs_x, abs_y, iw, ih, inp->bg_color);

                // Kenarlık varsa çiz
                if (inp->border_thickness > 0) {
                    // Dört bir kenara ince kenarlık çizgisi çizebilirsin veya etrafını sarabilirsin
                    gfx_fill_rect(abs_x, abs_y, iw, inp->border_thickness, inp->border_color); // Üst
                    gfx_fill_rect(abs_x, abs_y + ih - inp->border_thickness, iw, inp->border_thickness, inp->border_color); // Alt
                    gfx_fill_rect(abs_x, abs_y, inp->border_thickness, ih, inp->border_color); // Sol
                    gfx_fill_rect(abs_x + iw - inp->border_thickness, abs_y, inp->border_thickness, ih, inp->border_color); // Sağ
                }

                // Metin veya Placeholder çizimi
                if (inp->text[0] != '\0') {
                    gfx_draw_text_utf8(abs_x + 6, abs_y + 6, inp->text_color, inp->text);
                } else if (inp->placeholder[0] != '\0') {
                    gfx_draw_text_utf8(abs_x + 6, abs_y + 6, inp->placeholder_color, inp->placeholder);
                }

                // Eğer bu kutu şu an odaktaysa (active_input ise) sonuna imleç çizgisi (|) koy
                if (active_input == inp) {
                    // Metin uzunluğuna göre imlecin X koordinatını kaba taslak hesaplayabiliriz (her karakter ortalama 8 piksel)
                    int text_len = 0;
                    while(inp->text[text_len] != '\0') text_len++;
                    int cursor_pos_x = abs_x + 6 + (text_len * 8);
                    if (cursor_pos_x < abs_x + iw - 8) {
                        gfx_fill_rect(cursor_pos_x, abs_y + 6, 2, ih - 12, inp->text_color);
                    }
                }
            }
        }
    }

    // 10. Draw comboboxes (Açılır Menüler)
    for (int i = 0; i < win->combobox_count; i++) {
        combobox_t* cb = &win->comboboxes[i];
        int rel_x = cb->x;
        int rel_y = 24 + cb->y;
        int cw = cb->width;
        int ch = cb->height;

        // Kırpma (Clipping) kontrolleri
        if (rel_x < 0) { cw += rel_x; rel_x = 0; }
        if (rel_y < 24) { ch += (rel_y - 24); rel_y = 24; }

        if (rel_x < win->width && rel_y < win->height) {
            if (rel_x + cw > win->width) cw = win->width - rel_x;
            if (rel_y + ch > win->height) ch = win->height - rel_y;

            if (cw > 0 && ch > 0) {
                int abs_x = win->x + rel_x;
                int abs_y = win->y + rel_y;

                // Ana combobox kutusu arka planı ve çerçevesi
                gfx_fill_rect(abs_x, abs_y, cw, ch, cb->bg_color);
                
                // Basit çerçeve çizgileri
                gfx_fill_rect(abs_x, abs_y, cw, 1, cb->border_color); // Üst
                gfx_fill_rect(abs_x, abs_y + ch - 1, cw, 1, cb->border_color); // Alt
                gfx_fill_rect(abs_x, abs_y, 1, ch, cb->border_color); // Sol
                gfx_fill_rect(abs_x + cw - 1, abs_y, 1, ch, cb->border_color); // Sağ

                // Seçili öğe metnini çiz
                if (cb->items && cb->selected_index >= 0 && cb->selected_index < cb->item_count) {
                    gfx_draw_text_utf8(abs_x + 6, abs_y + 6, cb->text_color, cb->items[cb->selected_index]);
                }

                // Sağ tarafa ok işareti simgesi
                gfx_draw_text_utf8(abs_x + cw - 16, abs_y + 6, cb->text_color, "v");

                // Eğer menü açıksa (is_open == true), açılır listeyi çiz
                if (cb->is_open && cb->item_count > 0) {
                    int item_height = 20;
                    int list_height = cb->item_count * item_height;
                    int list_y = abs_y + ch;

                    // Liste arka planı (örneğin beyaz veya koyu tema rengi)
                    gfx_fill_rect(abs_x, list_y, cw, list_height, 0xFFFFFFFF);
                    
                    // Liste dış çerçevesi
                    gfx_fill_rect(abs_x, list_y, cw, list_height, cb->border_color);

                    // Öğeleri listele
                    for (int j = 0; j < cb->item_count; j++) {
                        int cur_item_y = list_y + (j * item_height);
                        
                        // Eğer fare bu öğenin üzerindeyse arka planı renklendirebilirsin
                        if (j == cb->selected_index) {
                            gfx_fill_rect(abs_x + 1, cur_item_y, cw - 2, item_height, 0xFF007ACC);
                            gfx_draw_text_utf8(abs_x + 6, cur_item_y + 4, 0xFFFFFFFF, cb->items[j]);
                        } else {
                            gfx_draw_text_utf8(abs_x + 6, cur_item_y + 4, 0xFF000000, cb->items[j]);
                        }
                    }
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

        // --- 1. BUTONLAR İÇİN HOVER KONTROLÜ ---
        for (int b = 0; b < win->button_count; b++) {
            button_t* btn = &win->buttons[b];
            
            int abs_x = win->x + btn->x;
            int abs_y = win->y + 24 + btn->y;

            if (mouse_x >= abs_x && mouse_x < abs_x + btn->width &&
                mouse_y >= abs_y && mouse_y < abs_y + btn->height) {
                
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

        // --- 2. PANELLER İÇİN HOVER KONTROLÜ ---
        for (int p_idx = 0; p_idx < win->panel_count; p_idx++) {
            panel_t* panel = &win->panels[p_idx];

            // EĞER panelin hover rengi yoksa (0 ise) VE on_hover callback'i de tanımlı değilse,
            // bu panel için hover tetiklemeye gerek yoktur!
            if (panel->hover_color == 0 && panel->on_hover == NULL) {
                continue;
            }

            int abs_x = win->x + panel->x;
            int abs_y = win->y + 24 + panel->y;

            if (mouse_x >= abs_x && mouse_x < abs_x + panel->width &&
                mouse_y >= abs_y && mouse_y < abs_y + panel->height) {
                
                if (!panel->is_hovered) {
                    panel->is_hovered = true;
                    damage_union_rect(abs_x, abs_y, panel->width, panel->height);
                }
            } else {
                if (panel->is_hovered) {
                    panel->is_hovered = false;
                    damage_union_rect(abs_x, abs_y, panel->width, panel->height);
                }
            }
        }

        // wm_update_hover_state fonksiyonunun uygun bir yerine veya sonuna ekleyin:
        for (int i = 0; i < window_count; i++) {
            window_t* win = &window_list[i];
            if (!win->is_active) continue;

            for (int c = 0; c < win->combobox_count; c++) {
                combobox_t* cb = &win->comboboxes[c];
                if (!cb->is_open || cb->item_count <= 0) continue;

                int abs_x = win->x + cb->x;
                int abs_y = win->y + 24 + cb->y;
                int item_height = 20;
                int list_height = cb->item_count * item_height;
                int list_y = abs_y + cb->height;

                // Fare açık olan açılır menünün üzerindeyse alanı kirli işaretle
                if (mouse_x >= abs_x && mouse_x < abs_x + cb->width &&
                    mouse_y >= list_y && mouse_y < list_y + list_height) {
                    damage_union_rect(abs_x, list_y, cb->width, list_height);
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
                // RESIZING ENDED: Lock reference (init) values to the new size!
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
                for (int i = 0; i < resized_window->label_count; i++) {
                    resized_window->labels[i].init_x = resized_window->labels[i].x;
                    resized_window->labels[i].init_y = resized_window->labels[i].y;
                    resized_window->labels[i].init_win_w = resized_window->width;
                    resized_window->labels[i].init_win_h = resized_window->height;
                }
                for (int i = 0; i < resized_window->combobox_count; i++) {
                    resized_window->comboboxes[i].init_x = resized_window->comboboxes[i].x;
                    resized_window->comboboxes[i].init_y = resized_window->comboboxes[i].y;
                    resized_window->comboboxes[i].init_width = resized_window->comboboxes[i].width;
                    resized_window->comboboxes[i].init_height = resized_window->comboboxes[i].height;
                    resized_window->comboboxes[i].init_win_w = resized_window->width;
                    resized_window->comboboxes[i].init_win_h = resized_window->height;
                }

                damage_union_rect(resize_old_x, resize_old_y, resize_old_w, resize_old_h);
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

            // --- IN-WINDOW BUTTON CLICK CHECK ---
            bool clicked_on_button = false;
            for (int i = 0; i < target->button_count; i++) {
                button_t* b = &target->buttons[i]; 
                int abs_bx = target->x + b->x;
                int abs_by = target->y + 24 + b->y;

                if (mouse_x >= abs_bx && mouse_x < abs_bx + b->width &&
                    mouse_y >= abs_by && mouse_y < abs_by + b->height) {
                    
                    serial_write("WM: In-window button clicked: ");
                    serial_write(b->text);
                    serial_write("\n");

                    if (b->on_click != 0) {
                        b->on_click();
                    }

                    clicked_on_button = true;
                    break;
                }
            }

            // --- IN-WINDOW TEXT INPUT FOCUS CHECK ---
            bool clicked_on_input = false;
            for (int i = 0; i < target->input_count; i++) {
                text_input_t* inp = &target->inputs[i];
                int abs_ix = target->x + inp->x;
                int abs_iy = target->y + 24 + inp->y;

                if (mouse_x >= abs_ix && mouse_x < abs_ix + inp->width &&
                    mouse_y >= abs_iy && mouse_y < abs_iy + inp->height) {
                    
                    active_input = inp;
                    clicked_on_input = true;
                    serial_write("WM: Text input focused\n");
                    break;
                }
            }

            // --- COMBOBOX CLICK CHECK ---
            bool clicked_on_combobox = false;
            for (int i = 0; i < target->combobox_count; i++) {
                combobox_t* cb = &target->comboboxes[i];
                int abs_cx = target->x + cb->x;
                int abs_cy = target->y + 24 + cb->y;

                // 1. Ana combobox kutusuna tıklandı mı?
                if (mouse_x >= abs_cx && mouse_x < abs_cx + cb->width &&
                    mouse_y >= abs_cy && mouse_y < abs_cy + cb->height) {
                    
                    // Diğer açık combobox'ları kapat
                    for (int k = 0; k < target->combobox_count; k++) {
                        if (k != i) target->comboboxes[k].is_open = false;
                    }

                    cb->is_open = !cb->is_open;
                    clicked_on_combobox = true;
                    
                    // BURASI EKLENDİ: Menü açılıp/kapanırken ekranı tazele
                    damage_union_rect(0, 0, fb_get_width(), fb_get_height());
                    desktop_redraw();

                    serial_write("WM: Combobox clicked\n");
                    break;
                }

                // 2. Menü açıksa ve açılan liste elemanlarına tıklandı mı?
                if (cb->is_open && cb->item_count > 0) {
                    int item_height = 20;
                    int list_height = cb->item_count * item_height;
                    int list_y = abs_cy + cb->height;

                    if (mouse_x >= abs_cx && mouse_x < abs_cx + cb->width &&
                        mouse_y >= list_y && mouse_y < list_y + list_height) {
                        
                        int clicked_index = (mouse_y - list_y) / item_height;
                        if (clicked_index >= 0 && clicked_index < cb->item_count) {
                            cb->selected_index = clicked_index;
                            cb->is_open = false;
                            
                            damage_union_rect(0, 0, fb_get_width(), fb_get_height());
                            desktop_redraw();

                            serial_write("WM: Combobox item selected\n");
                        }
                        clicked_on_combobox = true;
                        break;
                    }
                }
            }

            // Eğer combobox dışına tıklandıysa açık olanları kapat
            if (!clicked_on_combobox) {
                for (int k = 0; k < target->combobox_count; k++) {
                    target->comboboxes[k].is_open = false;
                }
            }

            // Eğer ne bir butona, ne bir inputa ne de bir combobox'a tıklandıysa aktif inputu sıfırla
            if (!clicked_on_input && !clicked_on_button && !clicked_on_combobox) {
                active_input = 0; 
            }

            // Resize check
            int dir = get_resize_direction(target, mouse_x, mouse_y);
            if (dir != RESIZE_NONE) {
                resized_window = target;
                resize_direction = dir;
                
                resize_old_x = target->x;
                resize_old_y = target->y;
                resize_old_w = target->width;
                resize_old_h = target->height;
            } else if (!clicked_on_button && !clicked_on_input && !clicked_on_combobox) {
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
            // Pencere dışına (masaüstüne) tıklandığında aktif inputu ve tüm açık combobox'ları kontrol et
            active_input = 0;
            bool any_combobox_was_open = false;
            
            for (int i = 0; i < window_count; i++) {
                for (int c = 0; c < window_list[i].combobox_count; c++) {
                    if (window_list[i].comboboxes[c].is_open) {
                        window_list[i].comboboxes[c].is_open = false;
                        any_combobox_was_open = true;
                    }
                }
            }

            // Eğer açık bir combobox dışarı tıklanarak kapatıldıysa tüm ekranı yenile
            if (any_combobox_was_open) {
                damage_union_rect(0, 0, fb_get_width(), fb_get_height());
            }

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
            } else if (any_combobox_was_open) {
                // Eğer etkin bir pencere yok ama sadece açık bir combobox kapatıldıysa yine de ekranı yeniden çiz
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

            // Use window-specific minimum limits
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

                // --- SOLUTION: Clear both the old area and new area during resizing ---
                damage_union_rect(resize_old_x, resize_old_y, resize_old_w, resize_old_h);
                
                resized_window->x = new_x;
                resized_window->y = new_y;
                resized_window->width = new_w;
                resized_window->height = new_h;

                // --- 1. ANCHOR CALCULATION FOR PANELS ---
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

                // --- 2. ANCHOR CALCULATION FOR BUTTONS ---
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

                // --- 3. ANCHOR CALCULATION FOR LABELS ---
                for (int i = 0; i < resized_window->label_count; i++) {
                    label_item_t* l = &resized_window->labels[i];

                    if (l->init_win_w == 0) l->init_win_w = resized_window->width;
                    if (l->init_win_h == 0) l->init_win_h = resized_window->height;

                    if (l->anchor & ANCHOR_RIGHT) {
                        int right_margin = l->init_win_w - l->init_x;
                        l->x = resized_window->width - right_margin;
                    }

                    if (l->anchor & ANCHOR_BOTTOM) {
                        int bottom_margin = l->init_win_h - l->init_y;
                        l->y = resized_window->height - bottom_margin;
                    }
                }

                // --- 4. ANCHOR CALCULATION FOR 3D CANVAS ---
                if (resized_window->has_canvas) {
                    // Eğer canvas yatayda veya dikeyde genişletilecekse (örneğin sağa/alta yaslıysa ya da tam kaplıyorsa)
                    // İhtiyacınıza göre burada canvas genişlik/yükseklik veya konum güncellemeleri yapabilirsiniz.
                    // Örneğin pencere boyutuna göre canvas boyutunu oranlamak isterseniz:
                    // resized_window->canvas_w = resized_window->canvas_w + (resized_window->width - old_w);
                    // resized_window->canvas_h = resized_window->canvas_h + (resized_window->height - old_h);
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

void wm_handle_key_press(char c) {
    if (!active_input) return;

    int len = 0;
    while (active_input->text[len] != '\0') len++;

    // Backspace (Geri silme tuşu)
    if (c == '\b' || c == 127) {
        if (len > 0) {
            active_input->text[len - 1] = '\0';
            damage_union_rect(0, 0, fb_get_width(), fb_get_height());
            desktop_redraw();
        }
        return;
    }

    // Normal yazdırılabilir karakterler
    if (c >= 32 && c < 127 && len < 126) {
        active_input->text[len] = c;
        active_input->text[len + 1] = '\0';
        damage_union_rect(0, 0, fb_get_width(), fb_get_height());
        desktop_redraw();
    }
}

window_t* wm_get_active_window(void) {
    return active_window;
}