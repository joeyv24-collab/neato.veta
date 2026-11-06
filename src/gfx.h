/* Tiny renderer interface. Two back ends implement it:
 *   gfx_gl.c   - vitaGL / OpenGL fixed function (the real game on Vita)
 *   gfx_soft.c - software rasteriser used by the desktop test build
 */
#ifndef GFX_H
#define GFX_H
#include <stdint.h>
#include "vmath.h"

#define SCREEN_W 960
#define SCREEN_H 544

typedef struct {
    float x, y, z;
    float u, v;
    uint32_t rgba;          /* bytes in memory: r, g, b, a */
} GVert;

enum {
    GFX_ALPHA_TEST = 1,     /* discard texels with alpha < 0.5 */
    GFX_BLEND      = 2,     /* standard alpha blending */
    GFX_NO_ZWRITE  = 4,
    GFX_NO_CULL    = 8,
    GFX_NO_ZTEST   = 16,
    GFX_NO_FOG     = 32,
    GFX_ADDITIVE   = 64,
};

static inline uint32_t rgba32(int r, int g, int b, int a) {
    return (uint32_t)(r & 255) | ((uint32_t)(g & 255) << 8) | ((uint32_t)(b & 255) << 16) | ((uint32_t)(a & 255) << 24);
}

int  gfx_init(void);
void gfx_shutdown(void);

/* textures; returns a handle > 0 */
int  gfx_texture(int w, int h, const uint8_t *rgba, int linear, int repeat);

/* static meshes uploaded once (world geometry). Returns handle > 0 */
int  gfx_mesh(const GVert *v, int nv, const uint16_t *idx, int ni);
void gfx_draw_mesh(int mesh, int tex, int flags);

/* dynamic geometry drawn straight from client memory */
void gfx_draw(const GVert *v, int nv, const uint16_t *idx, int ni, int tex, int flags);

void gfx_begin_frame(float r, float g, float b);
void gfx_set_matrices(const mat4 *proj, const mat4 *modelview);
void gfx_fog(float r, float g, float b, float start, float end);
void gfx_end_frame(void);

#endif
