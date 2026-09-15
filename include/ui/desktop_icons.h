#ifndef DESKTOP_ICONS_H
#define DESKTOP_ICONS_H

#include <stdint.h>
#include <stdbool.h>

void desktop_icons_init(void);
void desktop_icons_draw(int32_t mx, int32_t my, bool click_started);
int desktop_icons_get_selected(void);

#endif