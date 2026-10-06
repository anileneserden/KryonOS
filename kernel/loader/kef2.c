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

#define WM_TITLEBAR_HEIGHT 24

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

    uint32_t current_offset = sizeof(kef2_header_t); // 8 byte
    bool text_loaded = false;

    for (uint32_t i = 0; i < header->section_count; i++) {
        if (current_offset + sizeof(kef2_section_header_t) > file_size) {
            serial_write("KEFV2: Section header out of bounds.\n");
            kfree(file);
            return false;
        }

        kef2_section_header_t* sec = (kef2_section_header_t*)(file + current_offset);
        current_offset += sizeof(kef2_section_header_t);

        // Debug için seri porttan okunan tipi yazdırabilirsin:
        // char dbg_buf[64];
        // sprintf(dbg_buf, "KEFV2: Found section type: %u\n", sec->type);
        // serial_write(dbg_buf);

        if (sec->type == KEF2_SECTION_TEXT) {
            if (sec->offset + sec->size > file_size) {
                serial_write("KEFV2: Text section out of bounds.\n");
                kfree(file);
                return false;
            }

            if (KEF2_LOAD_ADDRESS + sec->size >= KEF_API_ADDRESS) {
                serial_write("KEFV2: Payload overlaps the API address.\n");
                kfree(file);
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
        kfree(file);
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
    kfree(file);
    return true;
}