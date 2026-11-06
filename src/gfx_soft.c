/* Software rasteriser implementing gfx.h. Only used by the desktop test build
 * so the game can be run and screenshotted without a Vita or a GPU. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"

typedef struct { int w, h, linear, repeat; uint8_t *px; } Tex;
typedef struct { GVert *v; uint16_t *i; int nv, ni; } SMesh;

#define MAXT 256
#define MAXM 1024
static Tex texs[MAXT]; static int ntex = 0;
static SMesh smeshes[MAXM]; static int nsm = 0;
static uint8_t fb[SCREEN_W * SCREEN_H * 3];
static float zb[SCREEN_W * SCREEN_H];
static mat4 P, MV, MVP;
static float fogc[3], fog0, fog1; static int fog_on;

int gfx_init(void) { return 1; }
void gfx_shutdown(void) {}

int gfx_texture(int w, int h, const uint8_t *rgba, int linear, int repeat) {
    Tex *t = &texs[++ntex];
    t->w = w; t->h = h; t->linear = linear; t->repeat = repeat;
    t->px = malloc(w * h * 4); memcpy(t->px, rgba, w * h * 4);
    return ntex;
}
int gfx_mesh(const GVert *v, int nv, const uint16_t *idx, int ni) {
    SMesh *m = &smeshes[++nsm];
    m->v = malloc(nv * sizeof(GVert)); memcpy(m->v, v, nv * sizeof(GVert));
    m->i = malloc(ni * 2); memcpy(m->i, idx, ni * 2);
    m->nv = nv; m->ni = ni;
    return nsm;
}

void gfx_begin_frame(float r, float g, float b) {
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        fb[i * 3] = (uint8_t)(r * 255); fb[i * 3 + 1] = (uint8_t)(g * 255); fb[i * 3 + 2] = (uint8_t)(b * 255);
        zb[i] = 1e30f;
    }
}
void gfx_set_matrices(const mat4 *p, const mat4 *mv) { P = *p; MV = *mv; MVP = m4mul(P, MV); }
void gfx_fog(float r, float g, float b, float s, float e) {
    fog_on = e > 0; fogc[0] = r; fogc[1] = g; fogc[2] = b; fog0 = s; fog1 = e;
}

typedef struct { float x, y, z, w; float u, v; float c[4]; float eye; } CV;

static void sample(Tex *t, float u, float v, float out[4]) {
    if (!t) { out[0] = out[1] = out[2] = out[3] = 1; return; }
    if (t->repeat) { u -= floorf(u); v -= floorf(v); }
    else { u = clampf(u, 0, 1); v = clampf(v, 0, 1); }
    float fx = u * t->w - 0.5f, fy = v * t->h - 0.5f;
    if (!t->linear) {
        int x = (int)(u * t->w), y = (int)(v * t->h);
        if (x >= t->w) x = t->w - 1; if (y >= t->h) y = t->h - 1; if (x < 0) x = 0; if (y < 0) y = 0;
        uint8_t *p = &t->px[(y * t->w + x) * 4];
        for (int k = 0; k < 4; k++) out[k] = p[k] / 255.0f;
        return;
    }
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float ax = fx - x0, ay = fy - y0;
    float acc[4] = {0, 0, 0, 0};
    for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) {
        int x = x0 + i, y = y0 + j;
        if (t->repeat) { x = ((x % t->w) + t->w) % t->w; y = ((y % t->h) + t->h) % t->h; }
        else { if (x < 0) x = 0; if (y < 0) y = 0; if (x >= t->w) x = t->w - 1; if (y >= t->h) y = t->h - 1; }
        float wgt = (i ? ax : 1 - ax) * (j ? ay : 1 - ay);
        uint8_t *p = &t->px[(y * t->w + x) * 4];
        for (int k = 0; k < 4; k++) acc[k] += p[k] / 255.0f * wgt;
    }
    memcpy(out, acc, sizeof acc);
}

static void raster(CV *a, CV *b, CV *c, Tex *t, int flags) {
    float sx[3], sy[3], iw[3]; CV *vs[3] = {a, b, c};
    for (int i = 0; i < 3; i++) {
        iw[i] = 1.0f / vs[i]->w;
        sx[i] = (vs[i]->x * iw[i] * 0.5f + 0.5f) * SCREEN_W;
        sy[i] = (1.0f - (vs[i]->y * iw[i] * 0.5f + 0.5f)) * SCREEN_H;
    }
    float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
    if (fabsf(area) < 1e-8f) return;
    /* screen y is flipped, so CCW (front) triangles have negative area here */
    if (!(flags & GFX_NO_CULL) && area > 0) return;
    int x0 = (int)floorf(fminf(sx[0], fminf(sx[1], sx[2]))), x1 = (int)ceilf(fmaxf(sx[0], fmaxf(sx[1], sx[2])));
    int y0 = (int)floorf(fminf(sy[0], fminf(sy[1], sy[2]))), y1 = (int)ceilf(fmaxf(sy[0], fmaxf(sy[1], sy[2])));
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > SCREEN_W - 1) x1 = SCREEN_W - 1; if (y1 > SCREEN_H - 1) y1 = SCREEN_H - 1;
    float inv = 1.0f / area;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        float px = x + 0.5f, py = y + 0.5f;
        float w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) * inv;
        float w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) * inv;
        float w2 = 1 - w0 - w1;
        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
        float z = w0 * a->z * iw[0] + w1 * b->z * iw[1] + w2 * c->z * iw[2];
        int o = y * SCREEN_W + x;
        if (!(flags & GFX_NO_ZTEST) && z > zb[o]) continue;
        float q0 = w0 * iw[0], q1 = w1 * iw[1], q2 = w2 * iw[2], qs = 1.0f / (q0 + q1 + q2);
        q0 *= qs; q1 *= qs; q2 *= qs;
        float col[4];
        sample(t, q0 * a->u + q1 * b->u + q2 * c->u, q0 * a->v + q1 * b->v + q2 * c->v, col);
        for (int k = 0; k < 4; k++) col[k] *= q0 * a->c[k] + q1 * b->c[k] + q2 * c->c[k];
        if ((flags & GFX_ALPHA_TEST) && col[3] < 0.5f) continue;
        if (fog_on && !(flags & GFX_NO_FOG)) {
            float e = q0 * a->eye + q1 * b->eye + q2 * c->eye;
            float f = clampf((fog1 - e) / (fog1 - fog0), 0, 1);
            for (int k = 0; k < 3; k++) col[k] = col[k] * f + fogc[k] * (1 - f);
        }
        uint8_t *d = &fb[o * 3];
        for (int k = 0; k < 3; k++) {
            float v = col[k];
            if (flags & GFX_ADDITIVE) v = d[k] / 255.0f + v * col[3];
            else if (flags & GFX_BLEND) v = d[k] / 255.0f * (1 - col[3]) + v * col[3];
            d[k] = (uint8_t)(clampf(v, 0, 1) * 255);
        }
        if (!(flags & GFX_NO_ZWRITE)) zb[o] = z;
    }
}

static CV cv_lerp(CV *a, CV *b, float t) {
    CV r;
    r.x = lerpf(a->x, b->x, t); r.y = lerpf(a->y, b->y, t); r.z = lerpf(a->z, b->z, t); r.w = lerpf(a->w, b->w, t);
    r.u = lerpf(a->u, b->u, t); r.v = lerpf(a->v, b->v, t); r.eye = lerpf(a->eye, b->eye, t);
    for (int k = 0; k < 4; k++) r.c[k] = lerpf(a->c[k], b->c[k], t);
    return r;
}

static void tri(const GVert *va, const GVert *vb, const GVert *vc, Tex *t, int flags) {
    CV in[3]; const GVert *src[3] = {va, vb, vc};
    for (int i = 0; i < 3; i++) {
        vec3 p = v3(src[i]->x, src[i]->y, src[i]->z);
        const float *m = MVP.m;
        in[i].x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
        in[i].y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
        in[i].z = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
        in[i].w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
        in[i].eye = -(MV.m[2] * p.x + MV.m[6] * p.y + MV.m[10] * p.z + MV.m[14]);
        in[i].u = src[i]->u; in[i].v = src[i]->v;
        uint32_t c = src[i]->rgba;
        for (int k = 0; k < 4; k++) in[i].c[k] = ((c >> (8 * k)) & 255) / 255.0f;
    }
    /* clip against near plane (z > -w) */
    CV out[4]; int n = 0;
    for (int i = 0; i < 3; i++) {
        CV *A = &in[i], *B = &in[(i + 1) % 3];
        float da = A->z + A->w, db = B->z + B->w;
        if (da >= 0) out[n++] = *A;
        if ((da >= 0) != (db >= 0)) out[n++] = cv_lerp(A, B, da / (da - db));
    }
    for (int i = 1; i + 1 < n; i++) raster(&out[0], &out[i], &out[i + 1], t, flags);
}

void gfx_draw(const GVert *v, int nv, const uint16_t *idx, int ni, int tex, int flags) {
    (void)nv;
    Tex *t = tex > 0 ? &texs[tex] : NULL;
    for (int i = 0; i + 2 < ni; i += 3) tri(&v[idx[i]], &v[idx[i + 1]], &v[idx[i + 2]], t, flags);
}
void gfx_draw_mesh(int mesh, int tex, int flags) {
    SMesh *m = &smeshes[mesh];
    gfx_draw(m->v, m->nv, m->i, m->ni, tex, flags);
}

void gfx_end_frame(void) {}

/* test-build only: dump the framebuffer */
void gfx_soft_save(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
    fwrite(fb, 1, sizeof fb, f);
    fclose(f);
}
