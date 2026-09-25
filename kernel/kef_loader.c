#include <kernel/kef.h>
#include <kernel/fs/vfs.h>
#include <kernel/mem/heap.h>
#include <kernel/serial.h>
#include <kernel/string.h>
#include <ui/wm.h>
#include <kernel/drivers/video/gfx.h>

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

    // Aynı şekilde arka plan rengi değişiminde de ekranın güncellenmesi gerekir
    damage_union_rect(win->x, win->y, win->width, win->height);
    wm_draw_window(win);
    desktop_redraw();
}

static void kef_exit(void) {
    serial_write("KEF: Application called the exit API; returning to the kernel.\n");
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

static int kef_get_directory_files(const char* full_path, vfs_file_info_t* out_list, int max_count) {
    serial_write("KEF API: get_directory_files called -> ");
    serial_write((char*)full_path);
    serial_write("\n");
    return vfs_get_directory_files(full_path, out_list, max_count);
}

static void* kef_read_file(const char* full_path, uint32_t* out_size) {
    serial_write("KEF API: read_file called -> ");
    serial_write((char*)full_path);
    serial_write("\n");
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

static void kef_canvas_update_buffer(int canvas_id) {
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

static void kef_install_api(void) {
    volatile kef_api_t* api = (volatile kef_api_t*)KEF_API_ADDRESS;
    api->window_create = kef_window_create;
    api->print = kef_print;
    api->exit = kef_exit;
    api->label_create = kef_label_create;
    api->panel_create = kef_panel_create;
    api->button_create = kef_button_create;
    api->get_directory_files = kef_get_directory_files;
    api->read_file = kef_read_file;
    api->strcmp = kef_strcmp;
    api->strlen = kef_strlen;
    api->yield = kef_yield;
    api->canvas_create = kef_canvas_create;
    api->canvas_update_buffer = kef_canvas_update_buffer;
    api->background_color = kef_background_color; // API Tablosuna bağlandı
}

bool kef_load_and_run(const char* path) {
    uint32_t file_size = 0;
    uint8_t* file = (uint8_t*)vfs_read_file(path, &file_size);

    if (!file || file_size < sizeof(kef_header_t)) {
        serial_write("KEF: file could not be read or is too small.\n");
        return false;
    }

    kef_header_t* header = (kef_header_t*)file;
    if (header->magic != KEF_MAGIC ||
        header->version != KEF_VERSION ||
        header->architecture != KEF_ARCH_I386 ||
        header->header_size < sizeof(kef_header_t) ||
        header->header_size > file_size ||
        header->payload_size > file_size - header->header_size ||
        header->entry_offset >= header->payload_size ||
        header->payload_size > KEF_MAX_SIZE) {
        serial_write("KEF: invalid or unsupported header.\n");
        kfree(file);
        return false;
    }

    if (KEF_LOAD_ADDRESS + header->payload_size >= KEF_API_ADDRESS) {
        serial_write("KEF: payload overlaps the API address.\n");
        kfree(file);
        return false;
    }

    uint8_t* payload = (uint8_t*)KEF_LOAD_ADDRESS;
    memcpy(payload, file + header->header_size, header->payload_size);

    kef_install_api();
    
    serial_write("KEF: Payload loaded to address: 0x400000\n");
    serial_write("KEF: file loaded, calling entry point.\n");
    kef_entry_t entry = (kef_entry_t)(payload + header->entry_offset);
    
    (void)entry();
    
    // UYGULAMA ÇALIŞTIKTAN / PENCERE OLUŞTURULDUKTAN SONRA İLK ÇİZİMİ TETİKLE:
    window_t* win = wm_get_active_window();
    if (win) {
        damage_union_rect(win->x, win->y, win->width, win->height);
        wm_draw_window(win);
        desktop_redraw();
    }

    serial_write("KEF: application returned.\n");
    kfree(file);
    return true;
}