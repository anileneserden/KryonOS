#ifndef UI_DESKTOP_H
#define UI_DESKTOP_H

#include <stdint.h>
#include <stdbool.h>

void desktop_init(void);

void damage_clear(void);
void damage_union_rect(int x, int y, int w, int h);
void desktop_redraw(void);

#endif