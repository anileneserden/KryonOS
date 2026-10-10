#include <kernel/loader/kef2.h>
#include <kernel/fs/vfs.h>
#include <kernel/mem/heap.h>
#include <kernel/serial.h>
#include <kernel/string.h>
#include <ui/wm.h>
#include <kernel/drivers/video/gfx.h>
#include <ui/desktop.h>
#include <kernel/power.h>
#include <kernel/drivers/input/keyboard_ps2.h>
#include <kernel/loader/png.h>

#define WM_TITLEBAR_HEIGHT 24

// Aktif KEF dosyasını ve kaynak tablosunu bellekte tutmak için global değişkenler
static uint8_t* current_kef_file_buffer = 0;
static uint32_t current_kef_file_size = 0;

static void kef_print(const char* str) {
    serial_write(str);
}

static int kef_window_create(const char* title, int width, int height) {
    window_t* window = wm_create_window(width, height, title);
    return window != 0;
}

static void kef_background_color(uint32_t color) {
    window_t* win = wm_get_active_window();
    if (!win) return;
    
    win->bg_color = color;
    damage_union_rect(win->x, win->y, win->width, win->height);
    wm_draw_window(win);
    desktop_redraw();
}

static void kef_exit(void) {
    serial_write("KEFV2: Application called the exit API; returning to the kernel.\n");
}

static int kef_panel_create(int x, int y, int w, int h, uint32_t color, uint32_t hover_color, void (*on_click)(void), void (*on_hover)(void), uint8_t anchor) {
    window_t* win = wm_get_active_window();
    if (!win) return -1;
    if (win->panel_count >= MAX_PANELS) return -1;

    panel_t* p = &win->panels[win->panel_count++];
    p->x = x; p->y = y; p->width = w; p->height = h; 
    p->color = color;
    p->hover_color = hover_color;     
    p->on_click = on_click;           
    p->on_hover = on_hover;           
    p->anchor = anchor;
    p->init_x = x; p->init_y = y; p->init_width = w; p->init_height = h;
    p->init_win_w = win->width; p->init_win_h = win->height;

    return 1;
}

static int kef_label_create(int x, int y, uint32_t color, const char* text, uint8_t anchor) {
    window_t* win = wm_get_active_window();
    if (!win) return -1;
    if (win->label_count >= MAX_LABELS) return -1;

    label_item_t* l = &win->labels[win->label_count++];
    l->x = x; l->y = y; l->color = color; l->anchor = anchor;
    l->init_x = x; l->init_y = y;
    l->init_win_w = win->width; l->init_win_h = win->height;
    
    int i = 0;
    while (text[i] != '\0' && i < 63) { l->text[i] = text[i]; i++; }
    l->text[i] = '\0';

    return 1;
}

static int kef_button_create(int x, int y, int w, int h, uint32_t bg_color, uint32_t text_color, const char* text, void (*on_click)(void), uint8_t anchor) {
    window_t* win = wm_get_active_window();
    if (!win) return -1;
    if (win->button_count >= MAX_BUTTONS) return -1;

    button_t* b = &win->buttons[win->button_count++];
    b->x = x; b->y = y; b->width = w; b->height = h;
    b->bg_color = bg_color; b->text_color = text_color; b->on_click = on_click;
    b->anchor = anchor;
    b->init_x = x; b->init_y = y; b->init_width = w; b->init_height = h;
    b->init_win_w = win->width; b->init_win_h = win->height;

    int i = 0;
    while (text[i] != '\0' && i < 31) { b->text[i] = text[i]; i++; }
    b->text[i] = '\0';

    wm_draw_window(win);
    return 1;
}

static uint8_t* kef_find_resource_in_memory(const char* target_filename, uint32_t* out_size) {
    if (!current_kef_file_buffer || current_kef_file_size < sizeof(kef2_header_t)) return 0;

    serial_write("[KEF DEBUG] Aranan kaynak: ");
    serial_write(target_filename);
    serial_write("\n");

    kef2_header_t* header = (kef2_header_t*)current_kef_file_buffer;
    uint32_t current_offset = sizeof(kef2_header_t);

    for (uint32_t i = 0; i < header->section_count; i++) {
        if (current_offset + sizeof(kef2_section_header_t) > current_kef_file_size) break;
        kef2_section_header_t* sec = (kef2_section_header_t*)(current_kef_file_buffer + current_offset);
        current_offset += sizeof(kef2_section_header_t);

        if (sec->type == KEF2_SECTION_RESOURCES) {
            uint8_t* section_ptr = current_kef_file_buffer + sec->offset;
            uint32_t section_left = sec->size;

            while (section_left > sizeof(uint32_t)) {
                char* fname = (char*)section_ptr;
                size_t fname_len = strlen(fname);
                if (fname_len == 0) break;

                serial_write("[KEF DEBUG] Paketteki kaynak: ");
                serial_write(fname);
                serial_write("\n");

                uint8_t* cursor = section_ptr + fname_len + 1;
                uint32_t consumed = fname_len + 1;

                if (consumed + sizeof(uint32_t) > section_left) break;

                uint32_t data_size = *(uint32_t*)cursor;
                cursor += sizeof(uint32_t);
                consumed += sizeof(uint32_t);

                if (consumed + data_size > section_left) break;

                uint8_t* data_ptr = cursor;
                cursor += data_size;
                consumed += data_size;

                if (consumed + 6 <= section_left) {
                    if (cursor[0] == 'I' && cursor[1] == 'M' && cursor[2] == 'G' &&
                        cursor[3] == 'E' && cursor[4] == 'N' && cursor[5] == 'D') {
                        cursor += 6;
                        consumed += 6;
                    }
                }

                section_left -= consumed;
                section_ptr = cursor;

                if (strcmp(fname, target_filename) == 0 || strstr(fname, target_filename) != NULL) {
                    serial_write("[KEF DEBUG] Kaynak basariyla eslesti!\n");
                    *out_size = data_size;
                    return data_ptr;
                }
            }
        }
    }
    serial_write("[KEF DEBUG] Kaynak bulunamadi!\n");
    return 0;
}

static int kef_picturebox_create(int x, int y, int w, int h, const char* img_path, uint8_t anchor) {
    window_t* win = wm_get_active_window();
    if (!win) return -1;
    if (win->picturebox_count >= MAX_PICTUREBOXES) return -1;

    picturebox_t* p = &win->pictureboxes[win->picturebox_count++];
    p->x = x; p->y = y; p->width = w; p->height = h;
    p->anchor = anchor;
    p->init_x = x; p->init_y = y; p->init_width = w; p->init_height = h;
    p->init_win_w = win->width; p->init_win_h = win->height;
    p->pixels = 0;
    p->img_width = 0;
    p->img_height = 0;
    
    int i = 0;
    while (img_path[i] != '\0' && i < 63) { p->image_path[i] = img_path[i]; i++; }
    p->image_path[i] = '\0';

    // 1. KEF paketinin içinden resmi bul
    uint32_t img_data_size = 0;
    uint8_t* img_raw_data = kef_find_resource_in_memory(img_path, &img_data_size);

    if (!img_raw_data || img_data_size == 0) {
        serial_write("[KEF ERROR] Picturebox resmi KEF paketinde bulunamadı: ");
        serial_write(img_path);
        serial_write("\n");
        return -1;
    }

    // 2. Ham PNG verisini bellekten decode et
    png_image_t* img = png_load_from_memory(img_raw_data, img_data_size);
    if (!img) {
        serial_write("[KEF ERROR] PNG bellekten decode edilemedi!\n");
        return -1;
    }

    // 3. Çözülen pikselleri ve boyutları picturebox yapısına aktar
    p->img_width = img->width;
    p->img_height = img->height;
    
    // Pikseller için bellek ayır ve kopyala
    size_t pixel_buf_size = img->width * img->height * sizeof(uint32_t);
    p->pixels = (uint32_t*)kmalloc(pixel_buf_size);
    if (p->pixels && img->pixels) {
        memcpy(p->pixels, img->pixels, pixel_buf_size);
    }

    png_free_image(img);

    wm_draw_window(win);
    return 1;
}

static int kef_input_create(int x, int y, int w, int h, 
                            const char* text, const char* placeholdertext, 
                            uint32_t backcolor, uint32_t color, 
                            uint32_t placeholder_color, uint32_t border_color, 
                            int border_thickness, uint8_t anchor) {
    window_t* win = wm_get_active_window();
    if (!win) return -1;
    if (win->input_count >= MAX_INPUTS) return -1;

    text_input_t* inp = &win->inputs[win->input_count++];
    inp->x = x; inp->y = y; inp->width = w; inp->height = h;
    inp->bg_color = backcolor; 
    inp->text_color = color;
    inp->placeholder_color = placeholder_color;
    inp->border_color = border_color;
    inp->border_thickness = border_thickness;
    inp->anchor = anchor;
    inp->init_x = x; inp->init_y = y; 
    inp->init_width = w; inp->init_height = h;
    inp->init_win_w = win->width; inp->init_win_h = win->height;

    int i = 0;
    if (text) {
        while (text[i] != '\0' && i < 127) { inp->text[i] = text[i]; i++; }
    }
    inp->text[i] = '\0';

    int j = 0;
    if (placeholdertext) {
        while (placeholdertext[j] != '\0' && j < 127) { inp->placeholder[j] = placeholdertext[j]; j++; }
    }
    inp->placeholder[j] = '\0';

    wm_draw_window(win);
    return 1;
}

static int kef_input_get_text(int input_id, char* out_buf, int max_len) {
    window_t* win = wm_get_active_window();
    if (!win || input_id < 0 || input_id >= win->input_count) return -1;

    text_input_t* inp = &win->inputs[input_id];
    int i = 0;
    while (inp->text[i] != '\0' && i < max_len - 1) {
        out_buf[i] = inp->text[i];
        i++;
    }
    out_buf[i] = '\0';
    return i;
}

static int kef_combobox_create(int x, int y, int w, int h, const char** items, int item_count, int default_index, uint32_t bg_color, uint32_t text_color, uint32_t border_color, uint8_t anchor) {
    window_t* win = wm_get_active_window();
    if (!win) return -1;
    if (win->combobox_count >= MAX_COMBOBOXES) return -1;

    combobox_t* cb = &win->comboboxes[win->combobox_count++];
    cb->x = x; cb->y = y; cb->width = w; cb->height = h;
    cb->items = items;
    cb->item_count = item_count;
    cb->selected_index = (default_index >= 0 && default_index < item_count) ? default_index : 0;
    cb->is_open = false;
    cb->bg_color = bg_color;
    cb->text_color = text_color;
    cb->border_color = border_color;
    cb->anchor = anchor;
    cb->init_x = x; cb->init_y = y; 
    cb->init_width = w; cb->init_height = h;
    cb->init_win_w = win->width; cb->init_win_h = win->height;

    wm_draw_window(win);
    return 1;
}

static int kef_get_directory_files(const char* full_path, void* out_list, int max_count) {
    return vfs_get_directory_files(full_path, out_list, max_count);
}

static void* kef_read_file(const char* full_path, uint32_t* out_size) {
    return vfs_read_file(full_path, out_size);
}

static int kef_strcmp(const char* s1, const char* s2) {
    return strcmp(s1, s2);
}

static size_t kef_strlen(const char* str) {
    return strlen(str);
}

static void kef_yield(void) {
    wm_process_input();
    __asm__ volatile("pause");
}

static uint32_t* kef_canvas_create(int x, int y, int w, int h, uint8_t anchor, int* out_canvas_id) {
    window_t* win = wm_get_active_window();
    if (!win || win->has_canvas) return 0;

    win->has_canvas = true;
    win->canvas_x = x;
    win->canvas_y = y;
    win->canvas_w = w;
    win->canvas_h = h;
    win->canvas_buffer = (uint32_t*)kmalloc(w * h * sizeof(uint32_t));
    win->canvas_anchor = anchor;

    if (out_canvas_id) *out_canvas_id = 1;
    return win->canvas_buffer;
}

static void kef_canvas_update_buffer(int canvas_id) {
    (void)canvas_id; 
    window_t* win = wm_get_active_window();
    if (!win || !win->has_canvas) return;

    damage_union_rect(win->x, win->y, win->width, win->height);
    wm_draw_window(win);
    desktop_redraw();
}

static void kef_reboot_system(void) { system_reboot(); }
static void kef_shutdown_system(void) { system_shutdown(); }
static bool kef_is_key_pressed(int key_code) { return keyboard_is_key_pressed(key_code); }

static void kef_install_api(void) {
    volatile kef_api_t* api = (volatile kef_api_t*)KEF_API_ADDRESS;
    api->window_create = kef_window_create;
    api->print = kef_print;
    api->exit = kef_exit;
    api->label_create = kef_label_create;
    api->panel_create = kef_panel_create;
    api->button_create = kef_button_create;
    api->picturebox_create = kef_picturebox_create;
    api->input_create = kef_input_create;
    api->input_get_text = kef_input_get_text;
    api->combobox_create = kef_combobox_create;
    api->get_directory_files = kef_get_directory_files;
    api->read_file = kef_read_file;
    api->strcmp = kef_strcmp;
    api->strlen = kef_strlen;
    api->yield = kef_yield;
    api->canvas_create = kef_canvas_create;
    api->canvas_update_buffer = kef_canvas_update_buffer;
    api->background_color = kef_background_color;
    api->reboot_system = kef_reboot_system;
    api->shutdown_system = kef_shutdown_system;
    api->is_key_pressed = kef_is_key_pressed;
}

// KEFv2 Yükleyici Fonksiyonu
bool kef2_load_and_run(const char* path) {
    uint32_t file_size = 0;
    uint8_t* file = (uint8_t*)vfs_read_file(path, &file_size);

    if (!file || file_size < sizeof(kef2_header_t)) {
        serial_write("KEFV2: File could not be read or is too small.\n");
        return false;
    }

    kef2_header_t* header = (kef2_header_t*)file;
    if (header->magic != KEF2_MAGIC || header->version != KEF2_VERSION) {
        serial_write("KEFV2: Invalid magic or version.\n");
        kfree(file);
        return false;
    }

    // Eski KEF buffer'ı varsa temizle, yenisini sakla
    if (current_kef_file_buffer) {
        kfree(current_kef_file_buffer);
    }
    current_kef_file_buffer = file;
    current_kef_file_size = file_size;

    uint32_t current_offset = sizeof(kef2_header_t);
    bool text_loaded = false;

    for (uint32_t i = 0; i < header->section_count; i++) {
        if (current_offset + sizeof(kef2_section_header_t) > file_size) {
            serial_write("KEFV2: Section header out of bounds.\n");
            return false;
        }

        kef2_section_header_t* sec = (kef2_section_header_t*)(file + current_offset);
        current_offset += sizeof(kef2_section_header_t);

        if (sec->type == KEF2_SECTION_TEXT) {
            if (sec->offset + sec->size > file_size) {
                serial_write("KEFV2: Text section out of bounds.\n");
                return false;
            }

            if (KEF2_LOAD_ADDRESS + sec->size >= KEF_API_ADDRESS) {
                serial_write("KEFV2: Payload overlaps the API address.\n");
                return false;
            }

            uint8_t* payload = (uint8_t*)KEF2_LOAD_ADDRESS;
            memcpy(payload, file + sec->offset, sec->size);
            text_loaded = true;
            
            serial_write("KEFV2: Text section loaded to 0x400000\n");
            break;
        }
    }

    if (!text_loaded) {
        serial_write("KEFV2: No TEXT section found in package!\n");
        return false;
    }

    kef_install_api();

    serial_write("KEFV2: Calling application entry point (0x400000).\n");
    kef_entry_t entry = (kef_entry_t)KEF2_LOAD_ADDRESS;
    (void)entry();

    window_t* win = wm_get_active_window();
    if (win) {
        damage_union_rect(win->x, win->y, win->width, win->height);
        wm_draw_window(win);
        desktop_redraw();
    }

    serial_write("KEFV2: Application returned.\n");
    return true;
}