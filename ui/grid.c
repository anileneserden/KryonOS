#include <ui/grid.h>

static int s_screen_w = 0;
static int s_screen_h = 0;
static int s_cell_w = 0;
static int s_cell_h = 0;
static int s_cols = 0;
static int s_rows = 0;
static int s_padding_x = 30;
static int s_padding_y = 30;
static int s_spacing = 10;

void grid_init(int screen_w, int screen_h, int cell_w, int cell_h) {
    s_screen_w = screen_w;
    s_screen_h = screen_h;
    s_cell_w = cell_w;
    s_cell_h = cell_h;

    if (cell_w + s_spacing > 0) {
        s_cols = (screen_w - (s_padding_x * 2) + s_spacing) / (cell_w + s_spacing);
        if (s_cols < 1) s_cols = 1;
    } else {
        s_cols = 1;
    }

    if (cell_h + s_spacing > 0) {
        s_rows = (screen_h - (s_padding_y * 2) + s_spacing) / (cell_h + s_spacing);
        if (s_rows < 1) s_rows = 1;
    } else {
        s_rows = 1;
    }
}

void grid_get_position(int index, int *out_x, int *out_y) {
    if (s_rows <= 0) {
        *out_x = s_padding_x;
        *out_y = s_padding_y;
        return;
    }

    // Windows tarzı: Yukarıdan aşağıya doldurup sütun kaydırma (column-major)
    int col = index / s_rows;
    int row = index % s_rows;

    *out_x = s_padding_x + (col * (s_cell_w + s_spacing));
    *out_y = s_padding_y + (row * (s_cell_h + s_spacing));
}

int grid_get_cols(void) { return s_cols; }
int grid_get_rows(void) { return s_rows; }
int grid_get_cell_width(void) { return s_cell_w; }
int grid_get_cell_height(void) { return s_cell_h; }