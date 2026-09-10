#ifndef KERNEL_APP_H
#define KERNEL_APP_H

#include <stdint.h>
#include <stdbool.h>
#include <ui/wm.h>

typedef struct app {
    int id;
    char name[32];
    window_t* window;
    
    // Yaşam döngüsü fonksiyon işaretçileri
    void (*init)(struct app* self);
    void (*update)(struct app* self);
    void (*draw)(struct app* self);
    void (*exit)(struct app* self);
    
    bool is_running;
} app_t;

#define MAX_APPS 16

void app_manager_init(void);
app_t* app_create(const char* name, int width, int height, void (*init)(app_t*), void (*draw)(app_t*));
void app_manager_update_all(void);

#endif