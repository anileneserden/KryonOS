#include <kernel/app.h>
#include <kernel/mem/heap.h> // kmalloc için
#include <kernel/serial.h>

static app_t* app_list[MAX_APPS];
static int app_count = 0;

void app_manager_init(void) {
    app_count = 0;
    for (int i = 0; i < MAX_APPS; i++) {
        app_list[i] = 0;
    }
    serial_write("App Manager baslatildi.\n");
}

app_t* app_create(const char* name, int width, int height, void (*init)(app_t*), void (*draw)(app_t*)) {
    if (app_count >= MAX_APPS) return 0;

    // kmalloc kullanıyoruz (heap aktif)
    app_t* app = (app_t*)kmalloc(sizeof(app_t));
    if (!app) return 0;

    app->id = app_count;
    
    int i = 0;
    while (name[i] != '\0' && i < 31) {
        app->name[i] = name[i];
        i++;
    }
    app->name[i] = '\0';

    // Pencereyi oluştur ve uygulamaya bağla
    app->window = wm_create_window(width, height, app->name);
    
    app->init = init;
    app->draw = draw;
    app->update = 0;
    app->exit = 0;
    app->is_running = true;

    if (app->init) {
        app->init(app);
    }

    app_list[app_count++] = app;
    return app;
}

void app_manager_update_all(void) {
    for (int i = 0; i < app_count; i++) {
        app_t* app = app_list[i];
        if (app && app->is_running && app->update) {
            app->update(app);
        }
    }
}