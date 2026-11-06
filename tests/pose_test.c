/* Renders the protagonist in every animation to check the code-driven poses. */
#include <stdio.h>
#include <stdlib.h>
#include "../src/gfx.h"
#include "../src/model.h"
#include "../src/anim.h"
#include "../src/platform.h"

void gfx_soft_save(const char *path);

static int load_tex(const char *n) {
    int sz; unsigned char *d = plat_load(n, &sz);
    int w = *(int *)(d + 4), h = *(int *)(d + 8);
    int t = gfx_texture(w, h, d + 12, 0, 0);
    free(d); return t;
}

int main(int argc, char **argv) {
    int which = argc > 1 ? atoi(argv[1]) : 0;
    float yaw = argc > 2 ? atof(argv[2]) : 0.6f;
    float tsec = argc > 3 ? atof(argv[3]) : 0.2f;
    float dist = argc > 4 ? atof(argv[4]) : 4.2f, ty = argc > 5 ? atof(argv[5]) : 0.85f;
    const char *outp = argc > 6 ? argv[6] : NULL;
    gfx_init();
    Model m; if (!model_load(&m, "neato.nmdl")) { printf("load fail\n"); return 1; }
    int tex[SLOT_COUNT] = {load_tex("neato_body.ntex"), load_tex("eye0.ntex"), load_tex("eyebrows0.ntex"),
                           load_tex("mouth0.ntex"), load_tex("pupil0.ntex")};
    Rig rig; anim_rig(&rig, &m);
    Animator a; anim_init(&a); anim_play(&a, (AnimId)which, 0);
    a.speed = which == ANIM_SPRINT ? 14 : 6;
    for (float t = 0; t < tsec; t += 1 / 60.0f) anim_update(&a, 1 / 60.0f);
    Pose pose; anim_apply(&rig, &a.out, &pose);
    model_skin(&m, &pose, v3norm(v3(0.4f, 0.8f, 0.5f)), 0.55f, 0xffffffff);
    gfx_begin_frame(0.0f, 1.0f, 0.0f);
    mat4 P = m4perspective(DEG2RAD(40), (float)SCREEN_W / SCREEN_H, 0.1f, 100);
    mat4 V = m4lookat(v3(dist * sinf(yaw), ty + 0.2f, dist * cosf(yaw)), v3(0, ty, 0), v3(0, 1, 0));
    mat4 W = m4mul(m4translate(v3(0, a.out.bob, 0)),
                   m4mul(m4translate(v3(0, 0.9f, 0)), m4mul(m4rotx(a.out.body_pitch), m4translate(v3(0, -0.9f, 0)))));
    mat4 MV = m4mul(V, W);
    gfx_set_matrices(&P, &MV);
    for (int s = 0; s < m.nsub; s++)
        gfx_draw(m.skinned, m.nverts, m.idx + m.sub[s].first, m.sub[s].count, tex[m.sub[s].slot],
                 GFX_ALPHA_TEST | GFX_NO_CULL);
    char out[64]; snprintf(out, sizeof out, "/tmp/pose_%02d.ppm", which);
    if (outp) snprintf(out, sizeof out, "%s", outp);
    gfx_soft_save(out);
    printf("%s bones_ok=%d\n", out, rig.bones_ok);
    return 0;
}
