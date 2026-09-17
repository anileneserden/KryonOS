#ifndef UI_CURSOR_H
#define UI_CURSOR_H

#include <stdint.h>
#include <stdbool.h>

#define CURSOR_WIDTH  16
#define CURSOR_HEIGHT 16

void cursor_init(void);
void cursor_update_and_redraw(void);
void cursor_hide(void); // Remove the cursor before drawing windows or the screen
void cursor_prepare_redraw(void); // Restore the cursor area to the back buffer without copying to VRAM
void cursor_get_position(int32_t* x, int32_t* y);
void cursor_show(void); // Draw the cursor again after drawing is complete
void cursor_refresh_background(void);
void cursor_sync_position(void);
void cursor_show_internal(bool blit);

int32_t cursor_get_x(void);
int32_t cursor_get_y(void);
int32_t cursor_get_old_x(void);
int32_t cursor_get_old_y(void);
int32_t cursor_get_width(void);
int32_t cursor_get_height(void);

#endif