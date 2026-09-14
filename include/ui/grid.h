#ifndef UI_GRID_H
#define UI_GRID_H

#include <stdint.h>

void grid_init(int screen_w, int screen_h, int cell_w, int cell_h);
void grid_get_position(int index, int *out_x, int *out_y);
int grid_get_cols(void);
int grid_get_rows(void);
int grid_get_cell_width(void);
int grid_get_cell_height(void);

#endif