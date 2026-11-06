/* Gameplay: player traversal, camera, effects and HUD.
 * Original setting: the flooded, quarantined island district of Saltreach. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "gfx.h"
#include "model.h"
#include "anim.h"
#include "world.h"

/* ------------------------------------------------------------------ */
/* tuning                                                              */

#define P_RADIUS     0.35f
#define P_HEIGHT     1.75f
#define RUN_SPEED    7.5f
#define SPRINT_SPEED 16.0f
#define GRAVITY      26.0f
#define JUMP_VEL     11.0f
#define WALLRUN_VEL  12.0f
#define GLIDE_SPEED  14.0f
#define GLIDE_SINK   2.4f
#define SLAM_VEL     38.0f
#define FOG_START    35.0f
#define FOG_END      115.0f

static const float FOG_RGB[3] = {0.62f, 0.66f, 0.70f};
static const float SKY_TOP[3] = {0.30f, 0.38f, 0.52f};

typedef enum { PS_GROUND, PS_AIR, PS_GLIDE, PS_WALLRUN, PS_LEDGE, PS_SLAM, PS_DODGE, PS_ATTACK, PS_LAND } PState;

typedef struct {
    vec3 pos, vel;
    float yaw;
    PState st; float st_t;
    int grounded;
    vec3 wall_n; int wall_touch;
    vec3 last_safe;
    float charge, health;
    int combo; int queued;
    float fall_speed;
    float off_wall_t;
    float body_pitch, pivot;
    float blink_t, blink;
    float respawn_fade;
    Animator anim;
    Pose pose;
} Player;

typedef struct {
    float yaw, pitch, dist;
    vec3 pos, target, fwd;
    float shake, idle_t;
} Camera;

typedef struct { vec3 p, v; float life, max_life, size; uint32_t col; int additive, ground; } Particle;
#define MAX_PART 384

static World world;
static Model hero;
static Rig rig;
static int tex_hero[SLOT_COUNT], tex_eye[3], tex_font, tex_dot, tex_ring;
static unsigned char font_w[96];
static Player pl;
static Camera cam;
static Particle parts[MAX_PART];
static float game_time, hint_t;
static float fps_acc; static int fps_frames, fps_show;
static unsigned fx_rng = 777;
int game_show_hud = 1, game_show_player = 1;   /* toggled by the test harness */

/* ------------------------------------------------------------------ */
/* asset helpers                                                       */

static int load_ntex(const char *name, int linear) {
    int size;
    uint8_t *d = plat_load(name, &size);
    if (!d) return 0;
    uint32_t w, h;
    memcpy(&w, d + 4, 4); memcpy(&h, d + 8, 4);
    int t = gfx_texture((int)w, (int)h, d + 12, linear, 0);
    free(d);
    return t;
}

static int make_dot(int ring) {
    enum { S = 64 };
    static uint8_t px[S * S * 4];
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
        float dx = (x + 0.5f) / S * 2 - 1, dy = (y + 0.5f) / S * 2 - 1;
        float r = sqrtf(dx * dx + dy * dy), a;
        if (ring) a = clampf(1.0f - fabsf(r - 0.8f) / 0.18f, 0, 1);
        else a = clampf(1.0f - r, 0, 1), a = a * a;
        uint8_t *p = &px[(y * S + x) * 4];
        p[0] = p[1] = p[2] = 255; p[3] = (uint8_t)(a * 255);
    }
    return gfx_texture(S, S, px, 1, 0);
}

/* ------------------------------------------------------------------ */
/* effects                                                             */

static void fx_spawn(vec3 p, vec3 v, float life, float size, uint32_t col, int additive, int ground) {
    for (int i = 0; i < MAX_PART; i++) if (parts[i].life <= 0) {
        Particle *q = &parts[i];
        q->p = p; q->v = v; q->life = q->max_life = life; q->size = size; q->col = col;
        q->additive = additive; q->ground = ground;
        return;
    }
}

static void fx_burst(vec3 p, int n, float speed, uint32_t col) {
    for (int i = 0; i < n; i++) {
        vec3 d = v3norm(v3(rng_range(&fx_rng, -1, 1), rng_range(&fx_rng, 0.1f, 1), rng_range(&fx_rng, -1, 1)));
        fx_spawn(p, v3scale(d, speed * rng_range(&fx_rng, 0.4f, 1.0f)), rng_range(&fx_rng, 0.3f, 0.7f),
                 rng_range(&fx_rng, 0.12f, 0.3f), col, 1, 0);
    }
}

static void fx_shockwave(vec3 p, float size) {
    fx_spawn(v3add(p, v3(0, 0.05f, 0)), v3(size * 2.5f, 0, 0), 0.45f, 0.5f, rgba32(140, 230, 255, 255), 1, 1);
    fx_spawn(v3add(p, v3(0, 0.07f, 0)), v3(size * 1.4f, 0, 0), 0.6f, 0.3f, rgba32(255, 255, 255, 200), 0, 1);
    fx_burst(v3add(p, v3(0, 0.3f, 0)), 26, 9 * size, rgba32(120, 220, 255, 255));
    for (int i = 0; i < 14; i++) {   /* dust */
        float a = i / 14.0f * 2 * (float)M_PI;
        fx_spawn(v3add(p, v3(cosf(a) * 0.8f, 0.2f, sinf(a) * 0.8f)), v3(cosf(a) * 7 * size, 1.5f, sinf(a) * 7 * size),
                 0.8f, 0.9f, rgba32(170, 165, 155, 140), 0, 0);
    }
}

static void fx_update(float dt) {
    for (int i = 0; i < MAX_PART; i++) {
        Particle *q = &parts[i];
        if (q->life <= 0) continue;
        q->life -= dt;
        if (q->ground) { q->size += q->v.x * dt; continue; }    /* rings grow */
        q->p = v3add(q->p, v3scale(q->v, dt));
        q->v = v3scale(q->v, 1.0f - 2.5f * dt);
        q->v.y -= 4.0f * dt;
    }
}

static void fx_draw(const mat4 *proj, const mat4 *view) {
    static GVert v[MAX_PART * 4];
    static uint16_t idx[MAX_PART * 6];
    vec3 right = v3(view->m[0], view->m[4], view->m[8]), up = v3(view->m[1], view->m[5], view->m[9]);
    gfx_set_matrices(proj, view);
    for (int pass = 0; pass < 3; pass++) {           /* 0 rings, 1 alpha, 2 additive */
        int n = 0;
        for (int i = 0; i < MAX_PART; i++) {
            Particle *q = &parts[i];
            if (q->life <= 0) continue;
            int kind = q->ground ? 0 : (q->additive ? 2 : 1);
            if (kind != pass) continue;
            float f = q->life / q->max_life;
            uint32_t a = (uint32_t)(((q->col >> 24) & 255) * f);
            uint32_t c = (q->col & 0xffffff) | (a << 24);
            vec3 r, u;
            if (q->ground) { r = v3(q->size, 0, 0); u = v3(0, 0, q->size); }
            else { r = v3scale(right, q->size); u = v3scale(up, q->size); }
            vec3 p = q->p;
            vec3 c0 = v3sub(v3sub(p, r), u), c1 = v3sub(v3add(p, r), u), c2 = v3add(v3add(p, r), u), c3 = v3add(v3sub(p, r), u);
            GVert *o = &v[n * 4];
            o[0] = (GVert){c0.x, c0.y, c0.z, 0, 1, c}; o[1] = (GVert){c1.x, c1.y, c1.z, 1, 1, c};
            o[2] = (GVert){c2.x, c2.y, c2.z, 1, 0, c}; o[3] = (GVert){c3.x, c3.y, c3.z, 0, 0, c};
            uint16_t s = (uint16_t)(n * 4);
            uint16_t *ii = &idx[n * 6];
            ii[0] = s; ii[1] = s + 1; ii[2] = s + 2; ii[3] = s; ii[4] = s + 2; ii[5] = s + 3;
            n++;
        }
        if (!n) continue;
        int flags = GFX_NO_ZWRITE | GFX_NO_CULL | (pass == 2 || pass == 0 ? GFX_ADDITIVE : GFX_BLEND);
        gfx_draw(v, n * 4, idx, n * 6, pass == 0 ? tex_ring : tex_dot, flags);
    }
}

/* ------------------------------------------------------------------ */
/* text / HUD                                                          */

static GVert hud_v[4096];
static uint16_t hud_i[6144];
static int hud_n;

static void hud_quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t c) {
    if (hud_n >= 1000) return;
    GVert *o = &hud_v[hud_n * 4];
    o[0] = (GVert){x, y + h, 0, u0, v1, c}; o[1] = (GVert){x + w, y + h, 0, u1, v1, c};
    o[2] = (GVert){x + w, y, 0, u1, v0, c}; o[3] = (GVert){x, y, 0, u0, v0, c};
    uint16_t s = (uint16_t)(hud_n * 4), *ii = &hud_i[hud_n * 6];
    ii[0] = s; ii[1] = s + 1; ii[2] = s + 2; ii[3] = s; ii[4] = s + 2; ii[5] = s + 3;
    hud_n++;
}

static void hud_flush(int tex) {
    if (!hud_n) return;
    gfx_draw(hud_v, hud_n * 4, hud_i, hud_n * 6, tex, GFX_BLEND | GFX_NO_ZTEST | GFX_NO_ZWRITE | GFX_NO_CULL | GFX_NO_FOG);
    hud_n = 0;
}

/* font atlas: 16 x 6 cells of 16px, ASCII 32..127 */
static void hud_text(float x, float y, float scale, uint32_t c, const char *s) {
    float cw = 9.0f * scale, ch = 16.0f * scale;
    for (; *s; s++) {
        int k = (unsigned char)*s - 32;
        if (k < 0 || k >= 96) k = 0;
        float u = (k % 16) / 16.0f, v = (k / 16) / 6.0f;
        hud_quad(x + 1, y + 1, 16 * scale, ch, u, v, u + 1 / 16.0f, v + 1 / 6.0f, rgba32(0, 0, 0, (c >> 24) & 255));
        hud_quad(x, y, 16 * scale, ch, u, v, u + 1 / 16.0f, v + 1 / 6.0f, c);
        x += font_w[k] ? (font_w[k] + 1.5f) * scale : cw;
    }
}

static void hud_bar(float x, float y, float w, float h, float f, uint32_t col) {
    hud_quad(x - 2, y - 2, w + 4, h + 4, 0, 0, 0, 0, rgba32(10, 12, 16, 170));
    hud_quad(x, y, w * clampf(f, 0, 1), h, 0, 0, 0, 0, col);
}

/* ------------------------------------------------------------------ */
/* player                                                              */

static void set_state(PState s) { pl.st = s; pl.st_t = 0; }

static void player_spawn(vec3 p) {
    memset(&pl.vel, 0, sizeof pl.vel);
    pl.pos = p; pl.last_safe = p;
    set_state(PS_GROUND);
    pl.grounded = 1;
}

/* resolve horizontal overlap with boxes; returns the strongest wall normal */
static void collide_walls(void) {
    int ids[64];
    vec3 p = pl.pos;
    float step = pl.grounded ? 0.55f : 0.1f;
    int n = world_query(&world, v3(p.x - 1, p.y + step, p.z - 1), v3(p.x + 1, p.y + P_HEIGHT, p.z + 1), ids, 64);
    pl.wall_touch = 0;
    for (int i = 0; i < n; i++) {
        const Box *b = &world.box[ids[i]];
        if (b->mx.y <= pl.pos.y + step) continue;      /* low enough to step on */
        float cx = clampf(pl.pos.x, b->mn.x, b->mx.x), cz = clampf(pl.pos.z, b->mn.z, b->mx.z);
        float dx = pl.pos.x - cx, dz = pl.pos.z - cz, d2 = dx * dx + dz * dz;
        vec3 nrm;
        if (d2 < 1e-8f) {      /* centre inside: push out along the shallowest axis */
            float pen[4] = {pl.pos.x - b->mn.x, b->mx.x - pl.pos.x, pl.pos.z - b->mn.z, b->mx.z - pl.pos.z};
            int k = 0; for (int j = 1; j < 4; j++) if (pen[j] < pen[k]) k = j;
            nrm = k == 0 ? v3(-1, 0, 0) : k == 1 ? v3(1, 0, 0) : k == 2 ? v3(0, 0, -1) : v3(0, 0, 1);
            pl.pos = v3add(pl.pos, v3scale(nrm, pen[k] + P_RADIUS));
        } else if (d2 < P_RADIUS * P_RADIUS) {
            float d = sqrtf(d2);
            nrm = v3(dx / d, 0, dz / d);
            pl.pos = v3add(pl.pos, v3scale(nrm, P_RADIUS - d));
        } else continue;
        /* remove velocity into the wall */
        float vn = v3dot(pl.vel, nrm);
        if (vn < 0) pl.vel = v3sub(pl.vel, v3scale(nrm, vn));
        if (b->mx.y > pl.pos.y + 1.2f) { pl.wall_touch = 1; pl.wall_n = nrm; }
    }
}

static void face_towards(vec3 dir, float rate, float dt) {
    if (dir.x * dir.x + dir.z * dir.z < 1e-4f) return;
    float target = atan2f(dir.x, dir.z);
    float d = wrap_angle(target - pl.yaw);
    float s = rate * dt;
    pl.yaw = wrap_angle(pl.yaw + clampf(d, -s, s));
}

static vec3 facing(void) { return v3(sinf(pl.yaw), 0, cosf(pl.yaw)); }

static void land(void) {
    float impact = pl.fall_speed;
    pl.grounded = 1;
    pl.vel.y = 0;
    if (pl.st == PS_SLAM) {
        float power = 1.0f + pl.charge / 50.0f;
        fx_shockwave(pl.pos, power);
        cam.shake = 0.6f;
        pl.charge = clampf(pl.charge - 30, 0, 100);
        pl.vel.x = pl.vel.z = 0;
        set_state(PS_LAND);
        anim_play(&pl.anim, ANIM_LAND, 0.05f);
        return;
    }
    /* falling builds kinetic charge: Neato absorbs impacts */
    pl.charge = clampf(pl.charge + impact * 0.8f, 0, 100);
    if (impact > 20) {
        fx_shockwave(pl.pos, 0.6f);
        cam.shake = 0.35f;
        pl.vel.x *= 0.2f; pl.vel.z *= 0.2f;
        set_state(PS_LAND);
        anim_play(&pl.anim, ANIM_LAND, 0.05f);
    } else set_state(PS_GROUND);
}

static void player_update(const Input *in, float dt) {
    /* camera-relative stick direction */
    vec3 cf = v3norm(v3(cam.fwd.x, 0, cam.fwd.z)), cr = v3(-cf.z, 0, cf.x);
    float sx = fabsf(in->lx) > 0.18f ? in->lx : 0, sy = fabsf(in->ly) > 0.18f ? in->ly : 0;
    vec3 dir = v3add(v3scale(cf, -sy), v3scale(cr, sx));
    float mag = clampf(v3len(dir), 0, 1);
    if (mag > 0) dir = v3scale(dir, 1.0f / v3len(dir));
    int sprint = (in->held & BTN_R) != 0;
    pl.st_t += dt;

    switch (pl.st) {
    case PS_GROUND: case PS_LAND: case PS_ATTACK: {
        float speed = mag * (sprint ? SPRINT_SPEED : RUN_SPEED);
        float accel = sprint ? 30.0f : 45.0f;
        if (pl.st == PS_LAND) { speed = 0; if (pl.st_t > 0.28f) set_state(PS_GROUND); }
        if (pl.st == PS_ATTACK) {
            speed = 0;
            vec3 lunge = v3scale(facing(), pl.st_t < 0.12f ? 5.0f : 0.0f);
            pl.vel.x = lunge.x; pl.vel.z = lunge.z;
            if ((in->pressed & BTN_SQUARE) && pl.st_t > 0.1f) pl.queued = 1;
            if (pl.st_t > 0.34f) {
                if (pl.queued && pl.combo < 3) {
                    pl.queued = 0; pl.combo++;
                    if (mag > 0) face_towards(dir, 100, 1);
                    set_state(PS_ATTACK);
                    anim_play(&pl.anim, pl.combo == 1 ? ANIM_PUNCH_R : pl.combo == 2 ? ANIM_KICK : ANIM_PUNCH_L, 0.06f);
                    vec3 hand = v3add(pl.pos, v3add(v3scale(facing(), 0.8f), v3(0, 1.2f, 0)));
                    fx_burst(hand, 6, 4, rgba32(120, 220, 255, 255));
                } else { pl.combo = 0; set_state(PS_GROUND); }
            }
            break;
        }
        vec3 target = v3scale(dir, speed);
        pl.vel.x = approachf(pl.vel.x, target.x, accel * dt);
        pl.vel.z = approachf(pl.vel.z, target.z, accel * dt);
        if (mag > 0) face_towards(dir, sprint ? 9.0f : 13.0f, dt);
        if (sprint && mag > 0.5f) pl.charge = clampf(pl.charge + 3.0f * dt, 0, 100);
        if (pl.st != PS_GROUND) break;
        if (in->pressed & BTN_CROSS) {
            pl.vel.y = JUMP_VEL + (sprint ? 2.0f : 0);
            pl.grounded = 0;
            set_state(PS_AIR);
            anim_play(&pl.anim, ANIM_JUMP, 0.08f);
            fx_spawn(pl.pos, v3(0, 0, 0), 0.4f, 0.2f, rgba32(255, 255, 255, 150), 0, 1);
        } else if (in->pressed & BTN_CIRCLE) {
            vec3 d = mag > 0 ? dir : facing();
            face_towards(d, 100, 1);
            pl.vel = v3scale(d, 15);
            set_state(PS_DODGE);
            anim_play(&pl.anim, ANIM_DODGE, 0.05f);
        } else if (in->pressed & BTN_SQUARE) {
            if (mag > 0) face_towards(dir, 100, 1);
            pl.combo = 0; pl.queued = 0;
            set_state(PS_ATTACK);
            anim_play(&pl.anim, ANIM_PUNCH_L, 0.06f);
        } else if ((in->pressed & BTN_TRIANGLE) && pl.charge >= 15) {
            /* kinetic blast: spend charge for a cone of force */
            pl.charge -= 15;
            set_state(PS_ATTACK); pl.combo = 3;
            anim_play(&pl.anim, ANIM_BLAST, 0.05f);
            vec3 o = v3add(pl.pos, v3add(v3scale(facing(), 0.6f), v3(0, 1.3f, 0)));
            for (int i = 0; i < 20; i++)
                fx_spawn(o, v3add(v3scale(facing(), rng_range(&fx_rng, 14, 24)),
                                  v3(rng_range(&fx_rng, -2, 2), rng_range(&fx_rng, -1, 2), rng_range(&fx_rng, -2, 2))),
                         0.5f, 0.25f, rgba32(140, 230, 255, 255), 1, 0);
            cam.shake = 0.15f;
        }
        /* walked off a ledge */
        float fl = world_floor(&world, pl.pos.x, pl.pos.z, pl.pos.y + 0.05f, P_RADIUS);
        if (fl < pl.pos.y - 0.35f) { pl.grounded = 0; set_state(PS_AIR); }
        /* sprinting into a tall wall starts a wall run */
        if (pl.wall_touch && sprint && mag > 0.5f && v3dot(dir, pl.wall_n) < -0.6f) {
            set_state(PS_WALLRUN);
            pl.grounded = 0;
        }
    } break;

    case PS_DODGE: {
        float k = 1.0f - pl.st_t / 0.45f;
        vec3 f = facing();
        pl.vel.x = f.x * 15 * k; pl.vel.z = f.z * 15 * k;
        if (pl.st_t > 0.45f) set_state(pl.grounded ? PS_GROUND : PS_AIR);
    } break;

    case PS_AIR: case PS_GLIDE: {
        int glide = pl.st == PS_GLIDE;
        if (glide) {
            if (!(in->held & BTN_CROSS)) { set_state(PS_AIR); break; }
            if (mag > 0) face_towards(dir, 2.4f, dt);
            vec3 f = facing();
            pl.vel.x = approachf(pl.vel.x, f.x * GLIDE_SPEED, 20 * dt);
            pl.vel.z = approachf(pl.vel.z, f.z * GLIDE_SPEED, 20 * dt);
            pl.vel.y = fmaxf(pl.vel.y - GRAVITY * dt, -GLIDE_SINK);
            pl.charge = clampf(pl.charge + 1.5f * dt, 0, 100);
            if (((int)(game_time * 30)) % 2 == 0) {
                vec3 h = v3add(pl.pos, v3(0, 1.0f, 0));
                vec3 side = v3scale(v3(-f.z, 0, f.x), 0.9f);
                fx_spawn(v3add(h, side), v3(0, 0, 0), 0.35f, 0.1f, rgba32(140, 230, 255, 255), 1, 0);
                fx_spawn(v3sub(h, side), v3(0, 0, 0), 0.35f, 0.1f, rgba32(140, 230, 255, 255), 1, 0);
            }
        } else {
            pl.vel.y -= GRAVITY * dt;
            float hs = sqrtf(pl.vel.x * pl.vel.x + pl.vel.z * pl.vel.z);
            float cap = fmaxf(hs, RUN_SPEED);
            vec3 t = v3scale(dir, cap * mag);
            if (mag > 0) {
                pl.vel.x = approachf(pl.vel.x, t.x, 14 * dt);
                pl.vel.z = approachf(pl.vel.z, t.z, 14 * dt);
                face_towards(dir, 6, dt);
            }
            if ((in->pressed & BTN_CROSS) && pl.st_t > 0.12f) set_state(PS_GLIDE);
        }
        if (in->pressed & BTN_CIRCLE) {
            set_state(PS_SLAM);
            pl.vel = v3(0, -SLAM_VEL, 0);
            anim_play(&pl.anim, ANIM_SLAM, 0.05f);
            break;
        }
        if (pl.wall_touch && mag > 0.5f && v3dot(dir, pl.wall_n) < -0.6f && pl.st_t > 0.15f) set_state(PS_WALLRUN);
    } break;

    case PS_WALLRUN: {
        vec3 n = pl.wall_n;
        pl.yaw = atan2f(-n.x, -n.z);
        pl.vel = v3add(v3(0, WALLRUN_VEL, 0), v3scale(n, -2.0f));     /* stick to the wall */
        pl.charge = clampf(pl.charge + 2.0f * dt, 0, 100);
        if (mag < 0.3f || v3dot(dir, n) > -0.2f) pl.off_wall_t += dt; else pl.off_wall_t = 0;
        if (in->pressed & BTN_CROSS) {         /* kick off the wall */
            pl.vel = v3add(v3scale(n, 9), v3(0, 10, 0));
            pl.yaw = atan2f(n.x, n.z);
            set_state(PS_AIR);
            anim_play(&pl.anim, ANIM_JUMP, 0.05f);
            break;
        }
        if (pl.off_wall_t > 0.2f) { pl.vel = v3scale(n, 3); set_state(PS_AIR); break; }
        /* reached the top? no wall in front of the chest any more */
        vec3 chest = v3add(pl.pos, v3(0, 0.4f, 0));
        float t;
        if (!world_raycast(&world, chest, v3add(chest, v3scale(n, -0.9f)), &t, NULL)) {
            pl.pos = v3add(pl.pos, v3add(v3scale(n, -0.9f), v3(0, 0.45f, 0)));
            pl.vel = v3add(v3scale(n, -3.0f), v3(0, 3.0f, 0));
            set_state(PS_LEDGE);
            anim_play(&pl.anim, ANIM_LEDGE, 0.05f);
        }
    } break;

    case PS_LEDGE:
        pl.vel.y -= GRAVITY * dt;
        if (pl.st_t > 0.3f) set_state(PS_AIR);
        break;

    case PS_SLAM:
        pl.vel = v3(0, -SLAM_VEL, 0);
        if (pl.st_t < 0.12f) pl.vel.y = 4;   /* brief hang before the plunge */
        break;
    }

    /* ---- integrate and collide ---- */
    float prev_y = pl.pos.y;
    pl.pos.x += pl.vel.x * dt;
    pl.pos.z += pl.vel.z * dt;
    collide_walls();
    pl.pos.y += pl.vel.y * dt;
    if (pl.vel.y < 0) pl.fall_speed = -pl.vel.y;
    if (pl.st != PS_WALLRUN) {
        float floor_y = world_floor(&world, pl.pos.x, pl.pos.z, fmaxf(prev_y, pl.pos.y) + (pl.grounded ? 0.5f : 0.02f), P_RADIUS);
        if (pl.grounded) {
            if (floor_y > pl.pos.y - 0.4f) pl.pos.y = floor_y;      /* follow steps / snap down */
            else if (pl.st != PS_GROUND) pl.grounded = 0;            /* rolled / struck off a ledge */
        } else if (pl.pos.y <= floor_y && pl.vel.y <= 0) {
            pl.pos.y = floor_y;
            land();
        }
        if (!pl.grounded && (pl.st == PS_DODGE || pl.st == PS_ATTACK || pl.st == PS_LAND))
            pl.vel.y -= GRAVITY * dt;
    }
    if (pl.grounded && world_on_land(pl.pos.x, pl.pos.z) && pl.st == PS_GROUND)
        pl.last_safe = pl.pos;

    /* fell in the flood water */
    if (pl.pos.y < WATER_Y + 0.3f && !world_on_land(pl.pos.x, pl.pos.z)) {
        fx_burst(v3(pl.pos.x, WATER_Y, pl.pos.z), 20, 6, rgba32(200, 230, 255, 200));
        player_spawn(pl.last_safe);
        pl.respawn_fade = 1.0f;
        pl.health = clampf(pl.health - 10, 0, 100);
    }
    pl.respawn_fade = fmaxf(0, pl.respawn_fade - dt * 1.5f);
    if (pl.health < 100) pl.health = clampf(pl.health + 2 * dt, 0, 100);

    /* ---- animation ---- */
    float hs = sqrtf(pl.vel.x * pl.vel.x + pl.vel.z * pl.vel.z);
    float target_pitch = 0, pivot = 0.9f;
    switch (pl.st) {
    case PS_GROUND:
        pl.anim.speed = hs;
        if (hs < 0.5f) anim_play(&pl.anim, ANIM_IDLE, 0.2f);
        else if (hs > RUN_SPEED + 1.5f) anim_play(&pl.anim, ANIM_SPRINT, 0.15f);
        else anim_play(&pl.anim, ANIM_RUN, 0.15f);
        break;
    case PS_AIR:
        if (pl.anim.cur != ANIM_JUMP || pl.vel.y < 0) anim_play(&pl.anim, ANIM_FALL, 0.3f);
        break;
    case PS_GLIDE: anim_play(&pl.anim, ANIM_GLIDE, 0.2f); break;
    case PS_WALLRUN:
        pl.anim.speed = WALLRUN_VEL;
        anim_play(&pl.anim, ANIM_WALLRUN, 0.1f);
        target_pitch = -0.35f; pivot = 0.9f;      /* lean back, feet planted on the wall */
        break;
    case PS_DODGE: target_pitch = pl.st_t / 0.45f * 2 * (float)M_PI; pivot = 0.55f; break;
    default: break;
    }
    if (pl.st == PS_DODGE) pl.body_pitch = target_pitch;
    else {
        if (pl.body_pitch > (float)M_PI) pl.body_pitch -= 2 * (float)M_PI;
        pl.body_pitch = lerpf(pl.body_pitch, target_pitch, clampf(14 * dt, 0, 1));
    }
    pl.pivot = lerpf(pl.pivot, pivot, clampf(14 * dt, 0, 1));
    anim_update(&pl.anim, dt);

    /* blinking */
    pl.blink_t -= dt;
    if (pl.blink_t < 0) { pl.blink = 0.14f; pl.blink_t = rng_range(&fx_rng, 2.5f, 5.0f); }
    pl.blink -= dt;

    /* kinetic sparks while sprinting */
    if (pl.st == PS_GROUND && hs > RUN_SPEED + 3 && ((int)(game_time * 60)) % 3 == 0)
        fx_spawn(v3add(pl.pos, v3(rng_range(&fx_rng, -0.2f, 0.2f), 0.1f, rng_range(&fx_rng, -0.2f, 0.2f))),
                 v3(rng_range(&fx_rng, -1, 1), 1.5f, rng_range(&fx_rng, -1, 1)), 0.4f, 0.09f,
                 rgba32(140, 230, 255, 255), 1, 0);
}

/* ------------------------------------------------------------------ */
/* camera                                                              */

static void camera_update(const Input *in, float dt) {
    float rx = fabsf(in->rx) > 0.15f ? in->rx : 0, ry = fabsf(in->ry) > 0.15f ? in->ry : 0;
    cam.yaw -= rx * 2.8f * dt;
    cam.pitch = clampf(cam.pitch + ry * 1.9f * dt, -0.45f, 1.25f);
    if (rx == 0 && ry == 0) cam.idle_t += dt; else cam.idle_t = 0;
    /* drift behind the player when moving fast and the stick is idle */
    float hs = sqrtf(pl.vel.x * pl.vel.x + pl.vel.z * pl.vel.z);
    if (cam.idle_t > 0.8f && hs > 4 && pl.st != PS_WALLRUN) {
        float d = wrap_angle(pl.yaw - cam.yaw);
        cam.yaw = wrap_angle(cam.yaw + d * clampf(1.2f * dt, 0, 1));
    }
    float want = 4.4f;
    if (pl.st == PS_GLIDE || hs > RUN_SPEED + 2) want = 5.6f;
    if (pl.st == PS_WALLRUN) want = 6.5f;
    cam.dist = lerpf(cam.dist, want, clampf(3 * dt, 0, 1));
    vec3 tgt = v3add(pl.pos, v3(0, 1.45f, 0));
    if (pl.st == PS_WALLRUN) tgt = v3add(tgt, v3(0, 1.0f, 0));
    cam.target = v3lerp(cam.target, tgt, clampf(16 * dt, 0, 1));
    cam.fwd = v3(sinf(cam.yaw) * cosf(cam.pitch), -sinf(cam.pitch), cosf(cam.yaw) * cosf(cam.pitch));
    vec3 want_pos = v3sub(cam.target, v3scale(cam.fwd, cam.dist));
    float t;
    if (world_raycast(&world, cam.target, want_pos, &t, NULL))
        want_pos = v3add(cam.target, v3scale(v3sub(want_pos, cam.target), fmaxf(t - 0.08f, 0.1f)));
    if (want_pos.y < 0.3f && world_on_land(want_pos.x, want_pos.z)) want_pos.y = 0.3f;
    cam.pos = want_pos;
    cam.shake = fmaxf(0, cam.shake - dt);
}

/* ------------------------------------------------------------------ */

int game_init(void) {
    world_build(&world, 20261106);
    if (!model_load(&hero, "neato.nmdl")) return 0;
    anim_rig(&rig, &hero);
    tex_hero[SLOT_BODY] = load_ntex("neato_body.ntex", 1);
    tex_eye[0] = load_ntex("eye0.ntex", 1);
    tex_eye[1] = load_ntex("eye1.ntex", 1);
    tex_eye[2] = load_ntex("eye2.ntex", 1);
    tex_hero[SLOT_EYE] = tex_eye[0];
    tex_hero[SLOT_BROWS] = load_ntex("eyebrows0.ntex", 1);
    tex_hero[SLOT_MOUTH] = load_ntex("mouth0.ntex", 1);
    tex_hero[SLOT_PUPIL] = load_ntex("pupil0.ntex", 1);
    tex_font = load_ntex("font.ntex", 1);
    {
        int sz; unsigned char *fw = plat_load("font.w", &sz);
        if (fw && sz >= 96) memcpy(font_w, fw, 96);
        free(fw);
    }
    tex_dot = make_dot(0);
    tex_ring = make_dot(1);

    memset(&pl, 0, sizeof pl);
    anim_init(&pl.anim);
    player_spawn(world.spawn);
    pl.yaw = (float)M_PI;            /* face away from the monument */
    pl.health = 100; pl.charge = 20;
    pl.pivot = 0.9f;
    pl.blink_t = 2;
    cam.yaw = pl.yaw; cam.pitch = 0.22f; cam.dist = 4.4f;
    cam.target = v3add(pl.pos, v3(0, 1.45f, 0));
    cam.fwd = v3(0, 0, -1);
    return 1;
}

void game_update(const Input *in, float dt) {
    game_time += dt;
    hint_t += dt;
    camera_update(in, dt);
    player_update(in, dt);
    fx_update(dt);
}

static void draw_hero(const mat4 *proj, const mat4 *view) {
    mat4 W = m4mul(m4translate(pl.pos), m4roty(pl.yaw));
    W = m4mul(W, m4translate(v3(0, pl.pivot, 0)));
    W = m4mul(W, m4rotx(pl.body_pitch + pl.anim.out.body_pitch));
    W = m4mul(W, m4translate(v3(0, -pl.pivot + pl.anim.out.bob, 0)));
    anim_apply(&rig, &pl.anim.out, &pl.pose);
    /* light direction into model space */
    vec3 L = v3norm(v3(0.45f, 0.8f, 0.4f));
    mat4 Rinv = m4rot_inverse(W);
    model_skin(&hero, &pl.pose, m4dir(Rinv, L), 0.62f, 0xffffffff);
    mat4 MV = m4mul(*view, W);
    gfx_set_matrices(proj, &MV);
    tex_hero[SLOT_EYE] = pl.blink > 0 ? tex_eye[2] : (pl.blink > -0.05f ? tex_eye[1] : tex_eye[0]);
    for (int s = 0; s < hero.nsub; s++)
        gfx_draw(hero.skinned, hero.nverts, hero.idx + hero.sub[s].first, hero.sub[s].count,
                 tex_hero[hero.sub[s].slot], GFX_ALPHA_TEST | GFX_NO_CULL);

    /* blob shadow */
    float fy = world_floor(&world, pl.pos.x, pl.pos.z, pl.pos.y + 0.1f, 0.01f);
    if (fy > -100) {
        float h = pl.pos.y - fy, s = 0.55f / (1 + h * 0.15f);
        uint32_t c = rgba32(0, 0, 0, (int)(150 / (1 + h * 0.3f)));
        GVert q[4] = {{pl.pos.x - s, fy + 0.03f, pl.pos.z + s, 0, 1, c}, {pl.pos.x + s, fy + 0.03f, pl.pos.z + s, 1, 1, c},
                      {pl.pos.x + s, fy + 0.03f, pl.pos.z - s, 1, 0, c}, {pl.pos.x - s, fy + 0.03f, pl.pos.z - s, 0, 0, c}};
        uint16_t qi[6] = {0, 1, 2, 0, 2, 3};
        gfx_set_matrices(proj, view);
        if (pl.st != PS_WALLRUN) gfx_draw(q, 4, qi, 6, tex_dot, GFX_BLEND | GFX_NO_ZWRITE | GFX_NO_CULL);
    }
}

static const char *state_name(void) {
    static const char *n[] = {"", "", "GLIDE", "WALL RUN", "", "SLAM", "", "", ""};
    return n[pl.st];
}

void game_render(void) {
    gfx_begin_frame(FOG_RGB[0], FOG_RGB[1], FOG_RGB[2]);

    /* sky gradient */
    mat4 ortho = m4ortho(0, SCREEN_W, SCREEN_H, 0, -1, 1), ident = m4identity();
    gfx_set_matrices(&ortho, &ident);
    uint32_t top = rgba32((int)(SKY_TOP[0] * 255), (int)(SKY_TOP[1] * 255), (int)(SKY_TOP[2] * 255), 255);
    uint32_t hor = rgba32((int)(FOG_RGB[0] * 255), (int)(FOG_RGB[1] * 255), (int)(FOG_RGB[2] * 255), 255);
    GVert sky[4] = {{0, SCREEN_H * 0.6f, 0, 0, 0, hor}, {SCREEN_W, SCREEN_H * 0.6f, 0, 0, 0, hor},
                    {SCREEN_W, 0, 0, 0, 0, top}, {0, 0, 0, 0, 0, top}};
    uint16_t si[6] = {0, 1, 2, 0, 2, 3};
    gfx_draw(sky, 4, si, 6, world.tex_white, GFX_NO_ZTEST | GFX_NO_ZWRITE | GFX_NO_CULL | GFX_NO_FOG);

    /* 3D */
    vec3 shake = v3(0, 0, 0);
    if (cam.shake > 0) shake = v3scale(v3(rng_range(&fx_rng, -1, 1), rng_range(&fx_rng, -1, 1), rng_range(&fx_rng, -1, 1)), cam.shake * 0.25f);
    float fov = 62.0f + clampf((sqrtf(pl.vel.x * pl.vel.x + pl.vel.z * pl.vel.z) - RUN_SPEED) * 0.8f, 0, 10);
    mat4 proj = m4perspective(DEG2RAD(fov), (float)SCREEN_W / SCREEN_H, 0.15f, FOG_END + 40);
    mat4 view = m4lookat(v3add(cam.pos, shake), v3add(cam.target, shake), v3(0, 1, 0));
    gfx_fog(FOG_RGB[0], FOG_RGB[1], FOG_RGB[2], FOG_START, FOG_END);
    world_draw(&world, &proj, &view, cam.pos, cam.fwd, FOG_END);
    if (game_show_player) draw_hero(&proj, &view);
    fx_draw(&proj, &view);

    /* HUD */
    if (!game_show_hud) { gfx_end_frame(); return; }
    gfx_set_matrices(&ortho, &ident);
    hud_bar(24, 24, 220, 10, pl.health / 100.0f, rgba32(230, 70, 90, 255));
    hud_bar(24, 42, 220, 8, pl.charge / 100.0f, rgba32(110, 225, 255, 255));
    if (pl.respawn_fade > 0) hud_quad(0, 0, SCREEN_W, SCREEN_H, 0, 0, 0, 0, rgba32(200, 230, 255, (int)(pl.respawn_fade * 255)));
    hud_flush(world.tex_white);
    hud_text(252, 18, 1, rgba32(255, 255, 255, 230), "NEATO");
    hud_text(252, 36, 0.8f, rgba32(150, 230, 255, 230), "KINETIC");
    const char *sn = state_name();
    if (*sn) hud_text(SCREEN_W / 2 - 40, SCREEN_H - 60, 1, rgba32(150, 230, 255, 220), sn);
    if (hint_t < 12) {
        uint32_t c = rgba32(255, 255, 255, (int)(230 * clampf(12 - hint_t, 0, 1)));
        hud_text(24, SCREEN_H - 120, 0.9f, c, "X jump / hold in air: glide");
        hud_text(24, SCREEN_H - 102, 0.9f, c, "R sprint - into a wall: wall run");
        hud_text(24, SCREEN_H - 84, 0.9f, c, "O dodge / in air: ground slam");
        hud_text(24, SCREEN_H - 66, 0.9f, c, "[] strike   /\\ kinetic blast");
    }
    hud_text(24, SCREEN_H - 36, 0.8f, rgba32(255, 255, 255, 140), "SALTREACH - QUARANTINE ZONE 3");
    fps_frames++;
    char buf[32]; snprintf(buf, sizeof buf, "%d FPS", fps_show);
    hud_text(SCREEN_W - 90, 14, 0.8f, rgba32(255, 255, 255, 150), buf);
    hud_flush(tex_font);
    gfx_end_frame();
}

/* called once per second by the main loop helpers */
void game_fps_tick(float dt) {
    fps_acc += dt;
    if (fps_acc >= 1) { fps_show = fps_frames; fps_frames = 0; fps_acc -= 1; }
}

void game_debug(GameDebug *d) {
    d->x = pl.pos.x; d->y = pl.pos.y; d->z = pl.pos.z; d->yaw = pl.yaw; d->state = pl.st; d->charge = pl.charge;
}
