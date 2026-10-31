#include <ui/renderer3d.h>
#include <kernel/drivers/video/fb.h>

// Mutlak değer (absolute) yardımı için basit fonksiyon
static inline int abs_val(int val) {
    return val < 0 ? -val : val;
}

// renderer3d başlangıç fonksiyonu
void renderer3d_init(void) {
    // Çözünürlük ve bellek yönetimi doğrudan fb_* fonksiyonları üzerinden yönetiliyor.
}

// Pixel çizim fonksiyonu
static void draw_pixel(int x, int y, uint32_t color) {
    uint32_t width = fb_get_width();
    uint32_t height = fb_get_height();

    if (x < 0 || (uint32_t)x >= width || y < 0 || (uint32_t)y >= height)
        return;

    fb_putpixel(x, y, color);
}

// Bresenham Çizgi Çizme Algoritması
static void renderer3d_draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = abs_val(x1 - x0);
    int dy = abs_val(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        draw_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

// Basit 3D nokta izdüşümü (Doğrudan Tamsayı / Integer tabanlı test sürümü)
static void project_point(int x, int y, int z, camera_t *cam, int *screen_x, int *screen_y) {
    // Kameraya göre göreceli konumlar
    int rel_x = x - FROM_FIXED(cam->position.x);
    int rel_y = y - FROM_FIXED(cam->position.y);
    int rel_z = z - FROM_FIXED(cam->position.z);

    if (rel_z <= 1) {
        *screen_x = -1;
        *screen_y = -1;
        return;
    }

    uint32_t width = fb_get_width();
    uint32_t height = fb_get_height();

    // Görüş açısı ve küpü büyütmek için çarpan (FOV scale)
    int fov_scale = 400; 

    // Perspektif bölme
    int x_val = (rel_x * fov_scale) / rel_z;
    int y_val = (rel_y * fov_scale) / rel_z;

    *screen_x = ((int)width / 2) + x_val;
    *screen_y = ((int)height / 2) - y_val;
}

// Her karede çağrılacak ana çizim döngüsü
void renderer3d_draw_frame(camera_t *cam) {
    fb_clear(0x000000);

    // Sabit noktalı (TO_FIXED) değil, doğrudan ham tamsayı koordinatlar kullanalım:
    int vertices[8][3] = {
        {-80, -80,  100},
        { 80, -80,  100},
        { 80,  80,  100},
        {-80,  80,  100},
        {-80, -80,  260},
        { 80, -80,  260},
        { 80,  80,  260},
        {-80,  80,  260}
    };

    int screen_x[8], screen_y[8];

    for (int i = 0; i < 8; i++) {
        project_point(vertices[i][0], vertices[i][1], vertices[i][2], cam, &screen_x[i], &screen_y[i]);
    }

    int edges[12][2] = {
        {0,1}, {1,2}, {2,3}, {3,0}, // Ön yüz
        {4,5}, {5,6}, {6,7}, {7,4}, // Arka yüz
        {0,4}, {1,5}, {2,6}, {3,7}  // Bağlantılar
    };

    for (int i = 0; i < 12; i++) {
        int p0 = edges[i][0];
        int p1 = edges[i][1];

        if (screen_x[p0] != -1 && screen_y[p0] != -1 && 
            screen_x[p1] != -1 && screen_y[p1] != -1) {
            renderer3d_draw_line(screen_x[p0], screen_y[p0], screen_x[p1], screen_y[p1], 0x00FFFFFF);
        }
    }

    fb_swap();
}