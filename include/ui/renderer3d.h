#ifndef RENDERER3D_H
#define RENDERER3D_H

typedef int fixed_t;

#define TO_FIXED(x) ((fixed_t)((x) * 65536))
#define FROM_FIXED(x) ((x) >> 16)

typedef struct {
    fixed_t x, y, z;
} vec3_t;

typedef struct {
    vec3_t position;
    int yaw;
    int pitch;
} camera_t;

// Parametresiz güncellendi
void renderer3d_init(void);
void renderer3d_draw_frame(camera_t *cam);

#endif