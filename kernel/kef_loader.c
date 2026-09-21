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

static void kef_exit(void) {
    serial_write("KEF: Application called the exit API; returning to the kernel.\n");
}

static int kef_panel_create(int x, int y, int w, int h, uint32_t color) {
    window_t* win = wm_get_active_window();
    if (!win || win->panel_count >= MAX_PANELS) {
        serial_write("KEF API Error: Aktif pencere bulunamadi veya panel limiti dolu!\n");
        return -1;
    }

    panel_t* p = &win->panels[win->panel_count++];
    p->x = x;
    p->y = y;
    p->width = w;
    p->height = h;
    p->color = color;

    wm_draw_window(win);
    return 1;
}

static int kef_label_create(int x, int y, uint32_t color, const char* text) {
    window_t* win = wm_get_active_window();
    if (!win || win->label_count >= MAX_LABELS) {
        serial_write("KEF API Error: Aktif pencere bulunamadi veya label limiti dolu!\n");
        return -1;
    }

    label_item_t* l = &win->labels[win->label_count++];
    l->x = x;
    l->y = y;
    l->color = color;
    
    int i = 0;
    while (text[i] != '\0' && i < 63) {
        l->text[i] = text[i];
        i++;
    }
    l->text[i] = '\0';

    wm_draw_window(win);

    serial_write("KEF API: Pencere ici renkli label basariyla baglandi -> ");
    serial_write((char*)text);
    serial_write("\n");
    return 1;
}

static int kef_button_create(int x, int y, int w, int h, uint32_t bg_color, uint32_t text_color, const char* text) {
    window_t* win = wm_get_active_window();
    if (!win || win->button_count >= MAX_BUTTONS) {
        serial_write("KEF API Error: Aktif pencere bulunamadi veya buton limiti dolu!\n");
        return -1;
    }

    button_t* b = &win->buttons[win->button_count++];
    b->x = x;
    b->y = y;
    b->width = w;
    b->height = h;
    b->bg_color = bg_color;
    b->text_color = text_color;
    
    int i = 0;
    while (text[i] != '\0' && i < 31) {
        b->text[i] = text[i];
        i++;
    }
    b->text[i] = '\0';

    wm_draw_window(win);

    serial_write("KEF API: Pencere ici buton basariyla olusturuldu -> ");
    serial_write((char*)text);
    serial_write("\n");
    return 1;
}

static void kef_install_api(void) {
    volatile kef_api_t* api = (volatile kef_api_t*)KEF_API_ADDRESS;
    api->window_create = kef_window_create;
    api->print = kef_print;
    api->exit = kef_exit;
    api->label_create = kef_label_create;
    api->panel_create = kef_panel_create;
    api->button_create = kef_button_create; // Buton API'si kernel tarafına kaydedildi
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
        return false;
    }

    if (KEF_LOAD_ADDRESS + header->payload_size >= KEF_API_ADDRESS) {
        serial_write("KEF: payload overlaps the API address.\n");
        return false;
    }

    uint8_t* payload = (uint8_t*)KEF_LOAD_ADDRESS;
    memcpy(payload, file + header->header_size, header->payload_size);

    kef_install_api();
    
    serial_write("KEF: Payload loaded to address: 0x400000\n");
    serial_write("KEF: file loaded, calling entry point.\n");
    kef_entry_t entry = (kef_entry_t)(payload + header->entry_offset);
    
    (void)entry();
    
    serial_write("KEF: application returned.\n");
    kfree(file);
    return true;
}