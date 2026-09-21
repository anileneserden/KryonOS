#ifndef UI_WINDOW_H
#define UI_WINDOW_H

#include <stdint.h>
#include <stdbool.h>
#include <ui/wm.h> // Pencere yöneticisindeki tam donanımlı window_t tanımını buraya dahil ediyoruz

#define WINDOW_TITLE_HEIGHT 24

// Artık yerel window_t tanımı yok, wm.h içindeki ortak window_t kullanılıyor.
void window_draw(window_t* win);

#endif