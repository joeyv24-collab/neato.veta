/* vitaGL (OpenGL fixed-function) back end */
#include <stdlib.h>
#include <string.h>
#ifdef __vita__
#include <vitaGL.h>
#else
#include <GL/gl.h>
#endif
#include "gfx.h"

#define MAX_MESH 512
typedef struct { GLuint vbo, ibo; int ni; } Mesh;
static Mesh meshes[MAX_MESH];
static int nmesh = 0;
static int fog_on = 0;

int gfx_init(void) {
#ifdef __vita__
    vglInit(0x100000);
#endif
    glViewport(0, 0, SCREEN_W, SCREEN_H);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_TEXTURE_2D);
    glAlphaFunc(GL_GREATER, 0.5f);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    return 1;
}

void gfx_shutdown(void) {
#ifdef __vita__
    vglEnd();
#endif
}

int gfx_texture(int w, int h, const uint8_t *rgba, int linear, int repeat) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    return (int)t;
}

static void set_state(int tex, int flags) {
    glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
    if (flags & GFX_ALPHA_TEST) glEnable(GL_ALPHA_TEST); else glDisable(GL_ALPHA_TEST);
    if (flags & (GFX_BLEND | GFX_ADDITIVE)) {
        glEnable(GL_BLEND);
        if (flags & GFX_ADDITIVE) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else glDisable(GL_BLEND);
    glDepthMask((flags & GFX_NO_ZWRITE) ? GL_FALSE : GL_TRUE);
    if (flags & GFX_NO_CULL) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
    if (flags & GFX_NO_ZTEST) glDisable(GL_DEPTH_TEST); else glEnable(GL_DEPTH_TEST);
    if (fog_on && !(flags & GFX_NO_FOG)) glEnable(GL_FOG); else glDisable(GL_FOG);
}

int gfx_mesh(const GVert *v, int nv, const uint16_t *idx, int ni) {
    if (nmesh >= MAX_MESH - 1) return 0;
    Mesh *m = &meshes[++nmesh];
    glGenBuffers(1, &m->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    glBufferData(GL_ARRAY_BUFFER, nv * sizeof(GVert), v, GL_STATIC_DRAW);
    glGenBuffers(1, &m->ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, ni * sizeof(uint16_t), idx, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    m->ni = ni;
    return nmesh;
}

void gfx_draw_mesh(int mesh, int tex, int flags) {
    Mesh *m = &meshes[mesh];
    set_state(tex, flags);
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo);
    glVertexPointer(3, GL_FLOAT, sizeof(GVert), (void *)0);
    glTexCoordPointer(2, GL_FLOAT, sizeof(GVert), (void *)12);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(GVert), (void *)20);
    glDrawElements(GL_TRIANGLES, m->ni, GL_UNSIGNED_SHORT, (void *)0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void gfx_draw(const GVert *v, int nv, const uint16_t *idx, int ni, int tex, int flags) {
    (void)nv;
    set_state(tex, flags);
    glVertexPointer(3, GL_FLOAT, sizeof(GVert), &v->x);
    glTexCoordPointer(2, GL_FLOAT, sizeof(GVert), &v->u);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(GVert), &v->rgba);
    glDrawElements(GL_TRIANGLES, ni, GL_UNSIGNED_SHORT, idx);
}

void gfx_begin_frame(float r, float g, float b) {
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void gfx_set_matrices(const mat4 *proj, const mat4 *mv) {
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj->m);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(mv->m);
}

void gfx_fog(float r, float g, float b, float start, float end) {
    if (end <= 0) { fog_on = 0; return; }
    GLfloat c[4] = {r, g, b, 1.0f};
    glFogfv(GL_FOG_COLOR, c);
    glFogf(GL_FOG_START, start);
    glFogf(GL_FOG_END, end);
    fog_on = 1;
}

void gfx_end_frame(void) {
#ifdef __vita__
    vglSwapBuffers(GL_FALSE);
#endif
}
