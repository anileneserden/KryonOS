#ifndef UI_CURSOR_H
#define UI_CURSOR_H

#include <stdint.h>

#define CURSOR_WIDTH  16
#define CURSOR_HEIGHT 16

void cursor_init(void);
void cursor_update_and_redraw(void);
void cursor_hide(void); // Pencere/ekran çiziminden önce imleci silmek için
void cursor_prepare_redraw(void); // İmleci back-buffer'a geri yükler, VRAM'e kopyalamaz
void cursor_get_position(int32_t* x, int32_t* y);
void cursor_show(void); // Çizim bittikten sonra imleci tekrar çizmek için

#endif