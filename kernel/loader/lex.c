#include <kernel/loader/lex.h>
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

static void lex_print(const char* str) {
    serial_write(str);
}

static int lex_window_create(const char* title, int width, int height) {
    window_t* window = wm_create_window(width, height, title);
    return window != 0;
}

static void lex_background_color(uint32_t color) {
    window_t* win = wm_get_active_window();
    if (!win) return;
    
    win->bg_color = color;

    // Aynı şekilde arka plan rengi değişiminde de ekranın güncellenmesi gerekir
    damage_union_rect(win->x, win->y, win->width, win->height);
    wm_draw_window(win);
    desktop_redraw();
}

static void lex_exit(void) {
    serial_write("LEX: Application called the exit API; returning to the kernel.\n");
}

static int lex_panel_create(int x, int y, int w, int h, uint32_t color, uint32_t hover_color, void (*on_click)(void), void (*on_hover)(void), uint8_t anchor) {
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

static int lex_label_create(int x, int y, uint32_t color, const char* text, uint8_t anchor) {
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

static int lex_button_create(int x, int y, int w, int h, uint32_t bg_color, uint32_t text_color, const char* text, void (*on_click)(void), uint8_t anchor) {
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

static int lex_input_create(int x, int y, int w, int h, 
                            const char* text, const char* placeholdertext, 
                            uint32_t backcolor, uint32_t color, 
                            uint32_t placeholder_color, uint32_t border_color, 
                            int border_thickness, uint8_t anchor) {
    window_t* win = wm_get_active_window();
    if (!win) return -1;

    // Pencere yapındaki input sınır kontrolü (Örn: MAX_INPUTS veya benzeri bir limit)
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

    // Metni kopyala
    int i = 0;
    if (text) {
        while (text[i] != '\0' && i < 127) { inp->text[i] = text[i]; i++; }
    }
    inp->text[i] = '\0';

    // Placeholder metnini kopyala
    int j = 0;
    if (placeholdertext) {
        while (placeholdertext[j] != '\0' && j < 127) { inp->placeholder[j] = placeholdertext[j]; j++; }
    }
    inp->placeholder[j] = '\0';

    wm_draw_window(win);
    return 1;
}

static int lex_input_get_text(int input_id, char* out_buf, int max_len) {
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

static int lex_combobox_create(int x, int y, int w, int h, const char** items, int item_count, int default_index, uint32_t bg_color, uint32_t text_color, uint32_t border_color, uint8_t anchor) {
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

static int lex_get_directory_files(const char* full_path, vfs_file_info_t* out_list, int max_count) {
    serial_write("LEX API: get_directory_files called -> ");
    serial_write((char*)full_path);
    serial_write("\n");
    return vfs_get_directory_files(full_path, out_list, max_count);
}

static void* lex_read_file(const char* full_path, uint32_t* out_size) {
    serial_write("LEX API: read_file called -> ");
    serial_write((char*)full_path);
    serial_write("\n");
    return vfs_read_file(full_path, out_size);
}

static int lex_strcmp(const char* s1, const char* s2) {
    return strcmp(s1, s2);
}

static size_t lex_strlen(const char* str) {
    return strlen(str);
}

static void lex_yield(void) {
    wm_process_input();
    __asm__ volatile("pause");
}

static uint32_t* lex_canvas_create(int x, int y, int w, int h, uint8_t anchor, int* out_canvas_id) {
    window_t* win = wm_get_active_window();
    if (!win) return 0;

    if (win->has_canvas) return 0;

    win->has_canvas = true;
    win->canvas_x = x;
    win->canvas_y = y;
    win->canvas_w = w;
    win->canvas_h = h;
    win->canvas_buffer = (uint32_t*)kmalloc(w * h * sizeof(uint32_t));
    win->canvas_anchor = anchor;

    if (out_canvas_id) {
        *out_canvas_id = 1;
    }

    return win->canvas_buffer;
}

static void lex_canvas_update_buffer(int canvas_id) {
    (void)canvas_id; 
    window_t* win = wm_get_active_window();
    if (!win || !win->has_canvas) return;

    // 1. Pencerenin ekran üzerindeki alanını kirli (damage) olarak işaretle
    damage_union_rect(win->x, win->y, win->width, win->height);

    // 2. Pencereyi ve bileşenlerini yeniden çiz
    wm_draw_window(win);

    // 3. Masaüstünü ve ekranı tazele
    desktop_redraw();
}

static void lex_reboot_system(void) {
    system_reboot();
}

static void lex_shutdown_system(void) {
    system_shutdown();
}

static bool lex_is_key_pressed(int key_code) {
    return keyboard_is_key_pressed(key_code);
}

static void lex_install_api(void) {
    volatile lex_api_t* api = (volatile lex_api_t*)LEX_API_ADDRESS;
    api->window_create = lex_window_create;
    api->print = lex_print;
    api->exit = lex_exit;
    api->label_create = lex_label_create;
    api->panel_create = lex_panel_create;
    api->button_create = lex_button_create;
    api->input_create = lex_input_create;
    api->input_get_text = lex_input_get_text;
    api->combobox_create = lex_combobox_create;
    api->get_directory_files = lex_get_directory_files;
    api->read_file = lex_read_file;
    api->strcmp = lex_strcmp;
    api->strlen = lex_strlen;
    api->yield = lex_yield;
    api->canvas_create = lex_canvas_create;
    api->canvas_update_buffer = lex_canvas_update_buffer;
    api->background_color = lex_background_color;
    api->reboot_system = lex_reboot_system;
    api->shutdown_system = lex_shutdown_system;
    api->is_key_pressed = lex_is_key_pressed;
}

bool lex_load_and_run(const char* path) {
    uint32_t file_size = 0;
    uint8_t* file = (uint8_t*)vfs_read_file(path, &file_size);

    if (!file || file_size < sizeof(lex_header_t)) {
        serial_write("LEX: file could not be read or is too small.\n");
        return false;
    }

    lex_header_t* header = (lex_header_t*)file;
    if (header->magic != LEX_MAGIC ||
        header->version != LEX_VERSION ||
        header->architecture != LEX_ARCH_I386 ||
        header->header_size < sizeof(lex_header_t) ||
        header->header_size > file_size ||
        header->payload_size > file_size - header->header_size ||
        header->entry_offset >= header->payload_size ||
        header->payload_size > LEX_MAX_SIZE) {
        serial_write("LEX: invalid or unsupported header.\n");
        kfree(file);
        return false;
    }

    if (LEX_LOAD_ADDRESS + header->payload_size >= LEX_API_ADDRESS) {
        serial_write("LEX: payload overlaps the API address.\n");
        kfree(file);
        return false;
    }

    uint8_t* payload = (uint8_t*)LEX_LOAD_ADDRESS;
    memcpy(payload, file + header->header_size, header->payload_size);

    lex_install_api();
    
    serial_write("LEX: Payload loaded to address: 0x400000\n");
    serial_write("LEX: file loaded, calling entry point.\n");
    lex_entry_t entry = (lex_entry_t)(payload + header->entry_offset);
    
    (void)entry();
    
    // UYGULAMA ÇALIŞTIKTAN / PENCERE OLUŞTURULDUKTAN SONRA İLK ÇİZİMİ TETİKLE:
    window_t* win = wm_get_active_window();
    if (win) {
        damage_union_rect(win->x, win->y, win->width, win->height);
        wm_draw_window(win);
        desktop_redraw();
    }

    serial_write("LEX: application returned.\n");
    kfree(file);
    return true;
}