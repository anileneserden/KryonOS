#include <ui/grid.h>

static int s_screen_w = 0;
static int s_screen_h = 0;
static int s_cell_w = 0;
static int s_cell_h = 0;
static int s_cols = 0;
static int s_rows = 0;

// Çizgilerle hücrelerin birebir örtüşmesi için padding ve spacing sıfırlandı
static int s_padding_x = 0;
static int s_padding_y = 0;
static int s_spacing = 0;

void grid_init(int screen_w, int screen_h, int cell_w, int cell_h) {
    s_screen_w = screen_w;
    s_screen_h = screen_h;
    s_cell_w = cell_w;
    s_cell_h = cell_h;

    if (cell_w > 0) {
        s_cols = screen_w / cell_w;
        if (s_cols < 1) s_cols = 1;
    } else {
        s_cols = 1;
    }

    if (cell_h > 0) {
        s_rows = screen_h / cell_h;
        if (s_rows < 1) s_rows = 1;
    } else {
        s_rows = 1;
    }
}

void grid_get_position(int index, int *out_x, int *out_y) {
    if (out_x == 0 || out_y == 0) return;

    if (s_rows <= 0) {
        *out_x = 0;
        *out_y = 0;
        return;
    }

    // Column-major (Yukarıdan aşağıya dizilim, sütun sütun ilerleme)
    int col = index / s_rows;
    int row = index % s_rows;

    *out_x = col * s_cell_w;
    *out_y = row * s_cell_h;
}

int grid_get_cols(void) { return s_cols; }
int grid_get_rows(void) { return s_rows; }
int grid_get_cell_width(void) { return s_cell_w; }
int grid_get_cell_height(void) { return s_cell_h; }