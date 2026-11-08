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
#include "enemy.h"

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

typedef enum { PS_GROUND, PS_AIR, PS_GLIDE, PS_WALLRUN, PS_LEDGE, PS_SLAM, PS_DODGE, PS_ATTACK, PS_LAND, PS_ROLL } PState;

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
    int target, hit_done, chain, best_chain, takedowns;
    float chain_t, invuln, hurt_t, scan;
    float aim, fire_cd, skid_t, land_t;
    int sense;
    float jump_buf, tri_buf, coyote, bank, spin, last_yaw;
    int finisher;
    Animator anim;
    Pose pose;
} Player;

typedef struct {
    float yaw, pitch, dist;
    vec3 pos, target, fwd;
    float shake, idle_t;
} Camera;

typedef struct { vec3 p, v; float life, max_life, size, grow; uint32_t col; int additive, ground; } Particle;
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
static Enemies foes;
typedef enum { GS_TITLE, GS_INTRO, GS_PLAY, GS_PAUSE, GS_ENDING } GState;
static GState gstate = GS_TITLE;

/* ---- story missions ---- */
typedef struct { const char *title, *brief; } MissionDef;
static const MissionDef MISSIONS[] = {
    {"Rooftops",     "Test your new body: run up a tower, glide on the mist, then slam down."},
    {"First Strike", "Break the Vantage checkpoint south of the plaza."},
    {"Making Noise", "Break two more checkpoints. Let Vantage know you're here."},
    {"Ghost",        "Break a checkpoint unseen. Up for mist sense; smoke bombs blind guards."},
    {"Lockdown",     "Break every Vantage checkpoint in Zone 3."},
    {"The Warden",   "Storm Vantage field HQ in the southeast and take down the Warden."},
};
#define NUM_MISSIONS ((int)(sizeof MISSIONS / sizeof MISSIONS[0]))
static int mission, mstep, menu_sel, has_save, slam_done;
static float mission_card_t, mission_done_t, glide_time;

typedef struct { char magic[4]; int version, mission, cleared_mask, best_chain, takedowns; float charge; } SaveData;
static float banner_t; static char banner[64];
static float dead_t;
static float hitstop;      /* brief freeze on impact */

/* smoke powers */
#define SMOKE_COL  rgba32(62, 60, 74, 175)
#define MIST_COL   rgba32(196, 186, 236, 255)
#define BOLT_COST  5.0f
#define BOMB_COST  25.0f
typedef struct { vec3 p, v; float life; } Bolt;
typedef struct { vec3 p; float t; } Cloud;
static Bolt bolts[16];
static Cloud clouds[4];
int game_skip_intro = 0;

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
        q->p = p; q->v = v; q->life = q->max_life = life; q->size = size; q->col = col; q->grow = 0;
        q->additive = additive; q->ground = ground;
        return;
    }
}

/* a puff of smoke: grows, drifts up and fades */
static void fx_smoke(vec3 p, vec3 v, float life, float size) {
    for (int i = 0; i < MAX_PART; i++) if (parts[i].life <= 0) {
        Particle *q = &parts[i];
        q->p = p; q->v = v; q->life = q->max_life = life; q->size = size; q->grow = size * 1.6f;
        int g = 55 + (int)(rng_float(&fx_rng) * 25);
        q->col = rgba32(g, g - 2, g + 12, 170);
        q->additive = 0; q->ground = 0;
        return;
    }
}

static void fx_smoke_burst(vec3 p, int n, float speed, float size) {
    for (int i = 0; i < n; i++) {
        vec3 d = v3norm(v3(rng_range(&fx_rng, -1, 1), rng_range(&fx_rng, -0.2f, 1), rng_range(&fx_rng, -1, 1)));
        fx_smoke(v3add(p, v3scale(d, 0.3f)), v3scale(d, speed * rng_range(&fx_rng, 0.3f, 1.0f)), rng_range(&fx_rng, 0.6f, 1.3f), size * rng_range(&fx_rng, 0.6f, 1.2f));
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
    fx_spawn(v3add(p, v3(0, 0.05f, 0)), v3(size * 2.5f, 0, 0), 0.45f, 0.5f, MIST_COL, 1, 1);
    fx_spawn(v3add(p, v3(0, 0.07f, 0)), v3(size * 1.4f, 0, 0), 0.6f, 0.3f, rgba32(90, 85, 110, 200), 0, 1);
    fx_burst(v3add(p, v3(0, 0.3f, 0)), 12, 7 * size, MIST_COL);
    for (int i = 0; i < 16; i++) {   /* a ring of smoke rolling outwards */
        float a = i / 16.0f * 2 * (float)M_PI;
        fx_smoke(v3add(p, v3(cosf(a) * 0.8f, 0.3f, sinf(a) * 0.8f)), v3(cosf(a) * 7 * size, 0.8f, sinf(a) * 7 * size),
                 1.0f, 0.7f * size);
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
        if (q->grow > 0) { q->size += q->grow * dt; q->v.y += 0.8f * dt; }   /* smoke rises */
        else q->v.y -= 4.0f * dt;
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
        const float eu = 0.5f / 256.0f, ev = 0.5f / 96.0f;     /* half-texel inset */
        float u = (k % 16) / 16.0f + eu, v = (k / 16) / 6.0f + ev, u1 = u + 1 / 16.0f - 2 * eu, v1 = v + 1 / 6.0f - 2 * ev;
        hud_quad(x + 1, y + 1, 16 * scale, ch, u, v, u1, v1, rgba32(0, 0, 0, (c >> 24) & 255));
        hud_quad(x, y, 16 * scale, ch, u, v, u1, v1, c);
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
static void slam_damage(float power);

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
        slam_damage(power);
        slam_done = 1;
        cam.shake = 0.6f;
        pl.charge = clampf(pl.charge - 20, 0, 100);
        enemies_blind(&foes, pl.pos, 4.0f + power * 2, 1.5f);
        pl.vel.x = pl.vel.z = 0;
        set_state(PS_LAND);
        anim_play(&pl.anim, ANIM_LAND, 0.05f);
        return;
    }
    float hs = sqrtf(pl.vel.x * pl.vel.x + pl.vel.z * pl.vel.z);
    if (impact > 20 && hs > 5) {
        /* carry the momentum into a roll instead of stopping dead */
        fx_shockwave(pl.pos, 0.45f);
        cam.shake = 0.2f;
        pl.yaw = atan2f(pl.vel.x, pl.vel.z);
        set_state(PS_ROLL);
        anim_play(&pl.anim, ANIM_DODGE, 0.04f);
    } else if (impact > 20) {
        fx_shockwave(pl.pos, 0.6f);
        cam.shake = 0.35f;
        pl.vel.x *= 0.2f; pl.vel.z *= 0.2f;
        set_state(PS_LAND);
        anim_play(&pl.anim, ANIM_LAND, 0.05f);
    } else {
        set_state(PS_GROUND);
        if (impact > 7) { pl.land_t = 0.2f; anim_play(&pl.anim, ANIM_LAND_SOFT, 0.04f); }
        if (pl.jump_buf > 0) pl.jump_buf = 0.12f;     /* buffered jump fires next frame */
    }
}


/* ------------------------------------------------------------------ */
/* combat                                                              */

static void chain_add(int n) {
    pl.chain += n; pl.chain_t = 0;
    if (pl.chain > pl.best_chain) pl.best_chain = pl.chain;
    pl.charge = clampf(pl.charge + 3.0f + pl.chain * 0.3f, 0, 100);
}

static void show_banner(const char *t) {
    snprintf(banner, sizeof banner, "%s", t);
    banner_t = 3.0f;
}

static void check_cleared(int before) {
    int now = enemies_groups_cleared(&foes);
    if (now > before) {
        if (now >= NUM_CHECKPOINTS) show_banner("ZONE 3 CHECKPOINTS DOWN");
        else show_banner("CHECKPOINT BROKEN");
    }
}

static void face_point(vec3 p) {
    vec3 d = v3sub(p, pl.pos);
    if (d.x * d.x + d.z * d.z > 1e-4f) pl.yaw = atan2f(d.x, d.z);
}

static void counter_or_blast(void);

/* square: zip to the best guard in the stick direction and hit them.
 * Unaware guards approached from behind or from above get a silent takedown. */
static void start_strike(vec3 dir, float mag) {
    int t = enemies_target(&foes, pl.pos, mag > 0 ? dir : v3(0, 0, 0), pl.grounded ? 9.0f : 6.0f);
    pl.target = t; pl.hit_done = 0;
    if (t >= 0) {
        Enemy *e = &foes.e[t];
        face_point(e->pos);
        if (!enemy_aware(&foes, t)) {
            vec3 ef = v3(sinf(e->yaw), 0, cosf(e->yaw));
            vec3 to_me = v3norm(v3sub(pl.pos, e->pos));
            int behind = v3dot(ef, to_me) < -0.2f || e->blind > 0;
            int above = pl.pos.y > e->pos.y + 1.5f;
            float d = v3len(v3sub(e->pos, pl.pos));
            if ((behind && d < 3.5f) || (above && d < 6.0f)) {
                int before = enemies_groups_cleared(&foes);
                enemy_takedown(&foes, t);
                pl.takedowns++;
                pl.charge = clampf(pl.charge + 12, 0, 100);
                pl.pos = v3add(e->pos, v3scale(v3norm(v3sub(pl.pos, e->pos)), 0.9f));
                pl.pos.y = e->pos.y;
                pl.grounded = 1; pl.vel = v3(0, 0, 0);
                set_state(PS_ATTACK); pl.combo = 3; pl.hit_done = 1;
                anim_play(&pl.anim, ANIM_KICK, 0.05f);
                fx_burst(v3add(e->pos, v3(0, 1.2f, 0)), 10, 3, rgba32(160, 200, 255, 255));
                show_banner("SILENT TAKEDOWN");
                check_cleared(before);
                return;
            }
        }
    } else if (mag > 0) face_towards(dir, 100, 1);
    set_state(PS_ATTACK);
    pl.finisher = t >= 0 && pl.chain >= 4 && (pl.chain + 1) % 5 == 0;
    static const AnimId seq[4] = {ANIM_PUNCH_L, ANIM_PUNCH_R, ANIM_KICK, ANIM_PUNCH_L};
    anim_play(&pl.anim, pl.finisher ? ANIM_KICK : seq[pl.combo & 3], 0.05f);
    if (pl.finisher) pl.spin = 2 * (float)M_PI;
}

static void attack_update(const Input *in, vec3 dir, float mag, float dt) {
    (void)dt;
    vec3 f = facing();
    if (pl.target >= 0 && !pl.hit_done) {
        /* close the gap fast, like a kinetic dash */
        Enemy *e = &foes.e[pl.target];
        vec3 d = v3sub(e->pos, pl.pos); d.y = 0;
        float dist = v3len(d);
        face_point(e->pos);
        float want = dist > 1.1f ? fminf((dist - 1.0f) / fmaxf(0.14f - pl.st_t, 0.03f), 38.0f) : 0;
        pl.vel.x = f.x * want; pl.vel.z = f.z * want;
        if (want > 12 && ((int)(game_time * 60)) % 2 == 0)
            fx_smoke(v3add(pl.pos, v3(0, 1.0f, 0)), v3(0, 0.3f, 0), 0.5f, 0.25f);
        if (pl.st_t >= 0.12f) {
            pl.hit_done = 1;
            if (dist < 2.2f && enemy_alive(e)) {
                int fin = pl.finisher, kick = pl.combo == 2 || fin;
                int before = enemies_groups_cleared(&foes);
                if (fin) enemy_hit(&foes, pl.target, 3, pl.pos, 15.0f, 6.0f);
                else enemy_hit(&foes, pl.target, kick ? 2 : 1, pl.pos, kick ? 9.0f : 3.0f, kick ? 3.5f : 0);
                chain_add(1);
                hitstop = fin ? 0.12f : kick ? 0.07f : 0.045f;
                cam.shake = fmaxf(cam.shake, fin ? 0.4f : kick ? 0.22f : 0.1f);
                fx_burst(v3add(e->pos, v3(0, 1.3f, 0)), fin ? 16 : kick ? 10 : 6, fin ? 9 : kick ? 6 : 4, MIST_COL);
                fx_smoke_burst(v3add(e->pos, v3(0, 1.2f, 0)), fin ? 8 : 3, 3, 0.25f);
                if (fin) { fx_shockwave(e->pos, 0.5f); show_banner("SMOKE FINISHER"); }
                check_cleared(before);
            }
        }
    } else {
        vec3 lunge = v3scale(f, pl.st_t < 0.12f ? 5.0f : 0.0f);
        pl.vel.x = lunge.x; pl.vel.z = lunge.z;
    }
    if ((in->pressed & BTN_SQUARE) && pl.st_t > 0.08f) pl.queued = 1;
    if ((in->pressed & BTN_TRIANGLE) && enemies_counterable(&foes, pl.pos, 5.0f) >= 0) { counter_or_blast(); return; }
    if ((in->pressed & BTN_CIRCLE) && pl.st_t > 0.1f) {        /* cancel into a dodge */
        vec3 d = mag > 0 ? dir : v3scale(f, -1);
        face_towards(d, 100, 1);
        set_state(PS_DODGE);
        fx_smoke_burst(v3add(pl.pos, v3(0, 1.0f, 0)), 6, 2, 0.35f);
        return;
    }
    float len = pl.target >= 0 ? 0.3f : 0.34f;
    if (pl.st_t > len) {
        if (pl.queued) {
            pl.queued = 0; pl.combo = (pl.combo + 1) % 3;
            start_strike(dir, mag);
        } else { pl.combo = 0; set_state(PS_GROUND); }
    }
}

/* triangle: counter a guard who is winding up, otherwise a smoke bomb */
static void counter_or_blast(void) {
    int c = enemies_counterable(&foes, pl.pos, 5.0f);
    if (c >= 0) {
        Enemy *e = &foes.e[c];
        face_point(e->pos);
        vec3 d = v3sub(pl.pos, e->pos); d.y = 0;
        if (v3len(d) > 1.3f) pl.pos = v3add(e->pos, v3scale(v3norm(d), 1.2f));
        int before = enemies_groups_cleared(&foes);
        enemy_hit(&foes, c, 2, pl.pos, 10.0f, 4.0f);
        chain_add(2);
        pl.invuln = 0.5f;
        set_state(PS_ATTACK); pl.target = -1; pl.hit_done = 1; pl.combo = 0;
        anim_play(&pl.anim, ANIM_COUNTER, 0.04f);
        hitstop = 0.1f;
        fx_burst(v3add(e->pos, v3(0, 1.4f, 0)), 16, 6, rgba32(255, 255, 255, 255));
        cam.shake = 0.25f;
        check_cleared(before);
        return;
    }
    if (pl.charge < BOMB_COST) return;
    /* smoke bomb: a cloud that blinds guards and hides Neato */
    pl.charge -= BOMB_COST;
    set_state(PS_ATTACK); pl.combo = 0; pl.target = -1; pl.hit_done = 1;
    anim_play(&pl.anim, ANIM_THROW, 0.05f);
    for (int i = 0; i < 4; i++) if (clouds[i].t <= 0) { clouds[i].p = pl.pos; clouds[i].t = 7.0f; break; }
    enemies_blind(&foes, pl.pos, 6.5f, 4.0f);
    fx_smoke_burst(v3add(pl.pos, v3(0, 0.8f, 0)), 30, 6, 0.9f);
    cam.shake = 0.12f;
}

/* where the reticle points: a guard under the crosshair (aim assist) or the
 * first thing the camera ray hits */
static vec3 aim_point(void) {
    vec3 from = cam.pos, f = cam.fwd;
    int best = -1; float bestc = cosf(DEG2RAD(10));
    for (int i = 0; i < foes.n; i++) {
        Enemy *e = &foes.e[i];
        if (!enemy_alive(e) || !foes.group_active[e->group]) continue;
        vec3 d = v3sub(v3add(e->pos, v3(0, 1.3f, 0)), from);
        float l = v3len(d);
        if (l > 50 || l < 1) continue;
        float c = v3dot(v3scale(d, 1 / l), f);
        float tt;
        if (world_raycast(&world, from, v3add(e->pos, v3(0, 1.3f, 0)), &tt, NULL)) continue;   /* behind cover */
        if (c > bestc) { bestc = c; best = i; }
    }
    if (best >= 0) return v3add(foes.e[best].pos, v3(0, 1.3f, 0));
    vec3 far = v3add(from, v3scale(f, 60));
    float t;
    if (world_raycast(&world, from, far, &t, NULL)) return v3add(from, v3scale(f, 60 * t));
    return far;
}

static void fire_bolt(void) {
    vec3 f = facing(), right = v3(-f.z, 0, f.x);
    vec3 hand = v3add(pl.pos, v3add(v3scale(right, -0.35f), v3add(v3scale(f, 0.5f), v3(0, 1.35f, 0))));
    vec3 dir = v3norm(v3sub(aim_point(), hand));
    for (int i = 0; i < 16; i++) if (bolts[i].life <= 0) {
        bolts[i].p = hand; bolts[i].v = v3scale(dir, 48); bolts[i].life = 1.2f;
        break;
    }
    pl.charge -= BOLT_COST;
    pl.fire_cd = 0.22f;
    fx_smoke_burst(hand, 3, 2, 0.15f);
    cam.shake = fmaxf(cam.shake, 0.05f);
}

static void bolts_update(float dt) {
    for (int i = 0; i < 16; i++) {
        Bolt *b = &bolts[i];
        if (b->life <= 0) continue;
        b->life -= dt;
        vec3 next = v3add(b->p, v3scale(b->v, dt));
        int hit = 0;
        for (int k = 0; k < foes.n && !hit; k++) {
            Enemy *e = &foes.e[k];
            if (!enemy_alive(e) || !foes.group_active[e->group]) continue;
            /* distance from the guard's body axis to this step of the bolt */
            vec3 c = v3add(e->pos, v3(0, 1.1f, 0)), d = v3sub(next, b->p);
            float t = clampf(v3dot(v3sub(c, b->p), d) / fmaxf(v3dot(d, d), 1e-6f), 0, 1);
            vec3 q = v3add(b->p, v3scale(d, t));
            if (v3len(v3sub(q, c)) < 0.75f) {
                int before = enemies_groups_cleared(&foes);
                enemy_hit(&foes, k, 1, b->p, 6.0f, 1.5f);
                chain_add(1);
                fx_smoke_burst(q, 6, 3, 0.3f);
                fx_burst(q, 6, 4, MIST_COL);
                check_cleared(before);
                hit = 1;
            }
        }
        float t;
        if (!hit && world_raycast(&world, b->p, next, &t, NULL)) {
            fx_smoke_burst(v3lerp(b->p, next, t), 4, 2, 0.25f);
            hit = 1;
        }
        if (hit) { b->life = 0; continue; }
        b->p = next;
        fx_smoke(b->p, v3(0, 0.1f, 0), 0.45f, 0.14f);
    }
    for (int c = 0; c < 4; c++) {
        Cloud *cl = &clouds[c];
        if (cl->t <= 0) continue;
        cl->t -= dt;
        if (((int)(game_time * 30)) % 2 == 0) {
            float a = rng_range(&fx_rng, 0, 6.28f), r = rng_range(&fx_rng, 0, 5.0f);
            fx_smoke(v3add(cl->p, v3(cosf(a) * r, rng_range(&fx_rng, 0.2f, 2.2f), sinf(a) * r)),
                     v3(0, 0.15f, 0), 1.6f, 1.0f);
        }
        if (((int)(game_time * 4)) != ((int)((game_time - dt) * 4)))
            enemies_blind(&foes, cl->p, 5.5f, 1.2f);     /* keep choking anyone inside */
    }
}

static void slam_damage(float power) {
    int before = enemies_groups_cleared(&foes);
    float r = 4.0f + power * 2.5f;
    for (int i = 0; i < foes.n; i++) {
        Enemy *e = &foes.e[i];
        if (!enemy_alive(e)) continue;
        vec3 d = v3sub(e->pos, pl.pos);
        if (fabsf(d.y) > 2.0f) continue;
        d.y = 0;
        if (v3len(d) < r) { enemy_hit(&foes, i, 3, pl.pos, 12.0f, 6.0f); chain_add(1); }
    }
    check_cleared(before);
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
    pl.jump_buf = (in->pressed & BTN_CROSS) ? 0.15f : fmaxf(0, pl.jump_buf - dt);
    pl.tri_buf = (in->pressed & BTN_TRIANGLE) ? 0.18f : fmaxf(0, pl.tri_buf - dt);
    pl.coyote = fmaxf(0, pl.coyote - dt);
    /* hold L to aim, R fires smoke bolts */
    int can_aim = pl.st == PS_GROUND || pl.st == PS_AIR || pl.st == PS_LAND;
    pl.aim = approachf(pl.aim, (in->held & BTN_L) && can_aim ? 1.0f : 0.0f, dt * 8);
    pl.fire_cd = fmaxf(0, pl.fire_cd - dt);
    if (pl.aim > 0.5f) {
        pl.yaw = cam.yaw;
        sprint = 0;
        if ((in->held & BTN_R) && pl.fire_cd <= 0 && pl.charge >= BOLT_COST) fire_bolt();
    } else pl.charge = clampf(pl.charge + 5.0f * dt, 0, 100);      /* mist slowly gathers */
    if (in->pressed & BTN_UP) pl.sense = !pl.sense;
    /* a counter pressed a moment early still counts */
    if (pl.tri_buf > 0 && !(in->pressed & BTN_TRIANGLE) && pl.grounded &&
        (pl.st == PS_GROUND || pl.st == PS_ATTACK) && enemies_counterable(&foes, pl.pos, 5.0f) >= 0) {
        pl.tri_buf = 0;
        counter_or_blast();
    }

    switch (pl.st) {
    case PS_GROUND: case PS_LAND: case PS_ATTACK: {
        float speed = mag * (sprint ? SPRINT_SPEED : (pl.aim > 0.5f ? 4.0f : RUN_SPEED));
        float accel = sprint ? 30.0f : 45.0f;
        if (pl.st == PS_LAND) { speed = 0; if (pl.st_t > 0.28f) set_state(PS_GROUND); }
        if (pl.st == PS_ATTACK) {
            speed = 0;
            attack_update(in, dir, mag, dt);
            break;
        }
        vec3 target = v3scale(dir, speed);
        pl.vel.x = approachf(pl.vel.x, target.x, accel * dt);
        pl.vel.z = approachf(pl.vel.z, target.z, accel * dt);
        if (mag > 0 && pl.aim < 0.5f) face_towards(dir, sprint ? 9.0f : 13.0f, dt);
        /* skid when stopping or reversing at speed */
        float hs_now = sqrtf(pl.vel.x * pl.vel.x + pl.vel.z * pl.vel.z);
        if (pl.st == PS_GROUND && pl.skid_t <= 0 && hs_now > 9 &&
            (mag < 0.1f || v3dot(dir, v3scale(v3(pl.vel.x, 0, pl.vel.z), 1 / hs_now)) < -0.5f)) {
            pl.skid_t = 0.32f;
            anim_play(&pl.anim, ANIM_SKID, 0.05f);
            for (int i = 0; i < 4; i++) fx_smoke(v3add(pl.pos, v3(0, 0.1f, 0)), v3(rng_range(&fx_rng, -1, 1), 0.5f, rng_range(&fx_rng, -1, 1)), 0.6f, 0.2f);
        }
        pl.skid_t = fmaxf(0, pl.skid_t - dt);
        if (pl.st != PS_GROUND) break;
        if (pl.jump_buf > 0) {
            pl.jump_buf = 0;
            pl.vel.y = JUMP_VEL + (sprint ? 2.0f : 0);
            pl.grounded = 0;
            set_state(PS_AIR);
            anim_play(&pl.anim, ANIM_JUMP, 0.08f);
            fx_spawn(pl.pos, v3(0, 0, 0), 0.4f, 0.2f, rgba32(255, 255, 255, 150), 0, 1);
        } else if (in->pressed & BTN_CIRCLE) {
            /* mist dash: become smoke, pass through guards and blows */
            vec3 d = mag > 0 ? dir : facing();
            face_towards(d, 100, 1);
            set_state(PS_DODGE);
            fx_smoke_burst(v3add(pl.pos, v3(0, 1.0f, 0)), 6, 2, 0.35f);
        } else if ((in->pressed & BTN_SQUARE) && pl.aim < 0.5f) {
            pl.combo = 0; pl.queued = 0;
            start_strike(dir, mag);
        } else if (in->pressed & BTN_TRIANGLE) {
            counter_or_blast();
        }
        /* walked off a ledge */
        float fl = world_floor(&world, pl.pos.x, pl.pos.z, pl.pos.y + 0.05f, P_RADIUS);
        if (fl < pl.pos.y - 0.35f) { pl.grounded = 0; set_state(PS_AIR); pl.coyote = 0.13f; }
        /* running into something waist-high: vault onto it */
        if (pl.wall_touch == 0 && mag > 0.5f) {
            vec3 ahead = v3add(pl.pos, v3scale(dir, 0.55f));
            float top = world_floor(&world, ahead.x, ahead.z, pl.pos.y + 1.4f, 0.1f);
            if (top > pl.pos.y + 0.5f && top <= pl.pos.y + 1.4f &&
                world_raycast(&world, v3add(pl.pos, v3(0, 0.3f, 0)), v3add(ahead, v3(0, 0.3f, 0)), NULL, NULL)) {
                pl.pos = v3(ahead.x, top, ahead.z);
                set_state(PS_LAND); pl.st_t = 0.14f;
                anim_play(&pl.anim, ANIM_LEDGE, 0.04f);
                break;
            }
        }
        /* sprinting into a tall wall starts a wall run */
        if (pl.wall_touch && sprint && mag > 0.5f && v3dot(dir, pl.wall_n) < -0.6f) {
            set_state(PS_WALLRUN);
            pl.grounded = 0;
        }
    } break;

    case PS_DODGE: {      /* mist dash */
        vec3 f = facing();
        float k = pl.st_t < 0.22f ? 1.0f : 0.35f;
        pl.vel.x = f.x * 24 * k; pl.vel.z = f.z * 24 * k;
        fx_smoke(v3add(pl.pos, v3(rng_range(&fx_rng, -0.2f, 0.2f), rng_range(&fx_rng, 0.4f, 1.5f), rng_range(&fx_rng, -0.2f, 0.2f))),
                 v3(0, 0.2f, 0), 0.7f, 0.3f);
        if (pl.st_t > 0.3f) {
            fx_smoke_burst(v3add(pl.pos, v3(0, 1.0f, 0)), 5, 2, 0.3f);
            set_state(pl.grounded ? PS_GROUND : PS_AIR);
        }
    } break;

    case PS_ROLL: {       /* momentum roll after a big drop */
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
            if (((int)(game_time * 30)) % 2 == 0) {        /* mist streaming off her arms */
                vec3 h = v3add(pl.pos, v3(0, 1.0f, 0));
                vec3 side = v3scale(v3(-f.z, 0, f.x), 0.9f);
                fx_smoke(v3add(h, side), v3(0, 0.2f, 0), 0.6f, 0.14f);
                fx_smoke(v3sub(h, side), v3(0, 0.2f, 0), 0.6f, 0.14f);
            }
        } else {
            pl.vel.y -= GRAVITY * dt;
            float hs = sqrtf(pl.vel.x * pl.vel.x + pl.vel.z * pl.vel.z);
            float cap = fmaxf(hs, RUN_SPEED);
            vec3 t = v3scale(dir, cap * mag);
            if (mag > 0) {
                pl.vel.x = approachf(pl.vel.x, t.x, 14 * dt);
                pl.vel.z = approachf(pl.vel.z, t.z, 14 * dt);
                if (pl.aim < 0.5f) face_towards(dir, 6, dt);
            }
            if ((in->pressed & BTN_CROSS) && pl.coyote > 0) {            /* late jump off a ledge */
                pl.coyote = 0; pl.jump_buf = 0;
                pl.vel.y = JUMP_VEL + (sprint ? 2.0f : 0);
                set_state(PS_AIR);
                anim_play(&pl.anim, ANIM_JUMP, 0.06f);
            } else if ((in->pressed & BTN_CROSS) && pl.st_t > 0.12f) set_state(PS_GLIDE);
        }
        if (in->pressed & BTN_SQUARE) {                 /* dive at a guard below */
            int t = enemies_target(&foes, pl.pos, mag > 0 ? dir : v3(0, 0, 0), 8.0f);
            if (t >= 0) {
                pl.combo = 0; pl.queued = 0;
                start_strike(dir, mag);
                if (pl.st == PS_ATTACK) pl.vel.y = fminf(pl.vel.y, -10);
                break;
            }
        }
        /* grab and vault onto ledges within reach */
        if (pl.wall_touch && (mag > 0.3f ? v3dot(dir, pl.wall_n) < -0.3f : v3dot(pl.vel, pl.wall_n) < -1.0f)) {
            vec3 over = v3sub(pl.pos, v3scale(pl.wall_n, 0.7f));
            float top = world_floor(&world, over.x, over.z, pl.pos.y + 2.3f, 0.1f);
            if (top > pl.pos.y + 0.25f && top <= pl.pos.y + 2.3f) {
                pl.pos = v3(over.x, top, over.z);
                pl.vel = v3scale(pl.wall_n, -2.0f);
                pl.yaw = atan2f(-pl.wall_n.x, -pl.wall_n.z);
                pl.grounded = 1;
                set_state(PS_LAND);
                pl.st_t = 0.12f;
                anim_play(&pl.anim, ANIM_LEDGE, 0.04f);
                break;
            }
        }
        if ((in->pressed & BTN_CIRCLE) && pl.aim < 0.5f) {
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
        if (!pl.grounded && (pl.st == PS_DODGE || pl.st == PS_ROLL || pl.st == PS_ATTACK || pl.st == PS_LAND))
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
        if (pl.aim > 0.5f) anim_play(&pl.anim, ANIM_AIM, 0.1f);
        else if (pl.skid_t > 0) anim_play(&pl.anim, ANIM_SKID, 0.05f);
        else if (pl.land_t > 0) anim_play(&pl.anim, ANIM_LAND_SOFT, 0.04f);
        else if (hs < 0.5f) anim_play(&pl.anim, ANIM_IDLE, 0.2f);
        else if (hs > RUN_SPEED + 1.5f) anim_play(&pl.anim, ANIM_SPRINT, 0.15f);
        else anim_play(&pl.anim, ANIM_RUN, 0.15f);
        break;
    case PS_AIR:
        if (pl.aim > 0.5f) anim_play(&pl.anim, ANIM_AIM, 0.1f);
        else if (pl.anim.cur != ANIM_JUMP || pl.vel.y < 0) anim_play(&pl.anim, ANIM_FALL, 0.3f);
        break;
    case PS_GLIDE: anim_play(&pl.anim, ANIM_GLIDE, 0.2f); break;
    case PS_WALLRUN:
        pl.anim.speed = WALLRUN_VEL;
        anim_play(&pl.anim, ANIM_WALLRUN, 0.1f);
        target_pitch = -0.35f; pivot = 0.9f;      /* lean back, feet planted on the wall */
        break;
    case PS_ROLL: target_pitch = pl.st_t / 0.45f * 2 * (float)M_PI; pivot = 0.55f; break;
    case PS_DODGE: anim_play(&pl.anim, ANIM_SPRINT, 0.05f); pl.anim.speed = 24; break;
    default: break;
    }
    pl.land_t = fmaxf(0, pl.land_t - dt);
    if (pl.st == PS_ROLL) pl.body_pitch = target_pitch;
    else {
        if (pl.body_pitch > (float)M_PI) pl.body_pitch -= 2 * (float)M_PI;
        pl.body_pitch = lerpf(pl.body_pitch, target_pitch, clampf(14 * dt, 0, 1));
    }
    pl.pivot = lerpf(pl.pivot, pivot, clampf(14 * dt, 0, 1));
    /* bank into turns, more when fast or gliding */
    float yaw_rate = dt > 0 ? wrap_angle(pl.yaw - pl.last_yaw) / dt : 0;
    pl.last_yaw = pl.yaw;
    float bank_target = 0;
    if (pl.st == PS_GROUND && hs > 2) bank_target = clampf(-yaw_rate * hs * 0.012f, -0.3f, 0.3f);
    if (pl.st == PS_GLIDE) bank_target = clampf(-yaw_rate * 0.35f, -0.6f, 0.6f);
    pl.bank = lerpf(pl.bank, bank_target, clampf(8 * dt, 0, 1));
    pl.spin = fmaxf(0, pl.spin - dt * 26);
    anim_update(&pl.anim, dt);

    /* blinking */
    pl.blink_t -= dt;
    if (pl.blink_t < 0) { pl.blink = 0.14f; pl.blink_t = rng_range(&fx_rng, 2.5f, 5.0f); }
    pl.blink -= dt;

    /* smoke wisps off her heels while sprinting */
    if (pl.st == PS_GROUND && hs > RUN_SPEED + 3 && ((int)(game_time * 60)) % 3 == 0)
        fx_smoke(v3add(pl.pos, v3(rng_range(&fx_rng, -0.2f, 0.2f), 0.2f, rng_range(&fx_rng, -0.2f, 0.2f))),
                 v3(rng_range(&fx_rng, -0.5f, 0.5f), 0.6f, rng_range(&fx_rng, -0.5f, 0.5f)), 0.6f, 0.12f);
}

/* ------------------------------------------------------------------ */
/* camera                                                              */

static void camera_update(const Input *in, float dt) {
    float rx = fabsf(in->rx) > 0.15f ? in->rx : 0, ry = fabsf(in->ry) > 0.15f ? in->ry : 0;
    cam.yaw -= rx * 2.8f * dt;
    cam.pitch = clampf(cam.pitch + ry * 1.9f * dt, -0.45f, 1.25f);
    /* aiming levels the camera out so the reticle looks down the street, not at her feet */
    if (pl.aim > 0.1f && ry == 0 && cam.pitch > 0.03f) cam.pitch = approachf(cam.pitch, 0.03f, dt * 1.5f * pl.aim);
    if (rx == 0 && ry == 0) cam.idle_t += dt; else cam.idle_t = 0;
    /* drift behind the player when moving fast and the stick is idle */
    float hs = sqrtf(pl.vel.x * pl.vel.x + pl.vel.z * pl.vel.z);
    if (cam.idle_t > 0.8f && hs > 4 && pl.st != PS_WALLRUN) {
        float d = wrap_angle(pl.yaw - cam.yaw);
        /* don't swing round when she's running towards the camera */
        if (fabsf(d) < 1.75f) cam.yaw = wrap_angle(cam.yaw + d * clampf(1.2f * dt, 0, 1));
    }
    float want = 4.4f;
    if (pl.st == PS_GLIDE || hs > RUN_SPEED + 2) want = 5.6f;
    if (pl.st == PS_WALLRUN) want = 6.5f;
    want = lerpf(want, 2.3f, pl.aim);
    cam.dist = lerpf(cam.dist, want, clampf(3 * dt, 0, 1));
    vec3 tgt = v3add(pl.pos, v3(0, 1.45f, 0));
    if (pl.st == PS_WALLRUN) tgt = v3add(tgt, v3(0, 1.0f, 0));
    {   /* shift over her right shoulder while aiming */
        vec3 cr = v3(-cosf(cam.yaw), 0, sinf(cam.yaw));
        tgt = v3add(tgt, v3add(v3scale(cr, 0.65f * pl.aim), v3(0, 0.15f * pl.aim, 0)));
    }
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
    pl.yaw = 0;                      /* face north, away from the monument */
    pl.health = 100; pl.charge = 20;
    pl.pivot = 0.9f;
    pl.blink_t = 2;
    cam.yaw = pl.yaw; cam.pitch = 0.22f; cam.dist = 4.4f;
    cam.target = v3add(pl.pos, v3(0, 1.45f, 0));
    cam.fwd = v3(0, 0, -1);
    pl.target = -1;
    enemies_init(&foes, &world, 913);
    gstate = game_skip_intro ? GS_PLAY : GS_TITLE;
    { SaveData sd; has_save = plat_save_read(&sd, sizeof sd) && !memcmp(sd.magic, "NEAT", 4) && sd.version == 1; }
    menu_sel = has_save ? 0 : 1;
    mission_card_t = game_skip_intro ? 4.0f : 0;
    return 1;
}

static void player_defeated(void) {
    enemies_reset(&foes);
    player_spawn(world.spawn);
    pl.health = 100; pl.chain = 0;
    pl.respawn_fade = 1.0f;
    show_banner("NEATO WAS DOWNED");
    dead_t = 0;
}

static void combat_update(float dt) {
    PlayerInfo pi;
    pi.pos = pl.pos;
    pl.invuln = fmaxf(0, pl.invuln - dt);
    pi.invulnerable = pl.invuln > 0 || pl.st == PS_DODGE || (pl.st == PS_ROLL && pl.st_t < 0.38f) || pl.st == PS_LEDGE ||
                      pl.st == PS_SLAM || pl.st == PS_WALLRUN;
    pi.hidden = pl.pos.y > 3.0f;
    for (int c = 0; c < 4; c++) if (clouds[c].t > 0 && v3len(v3sub(clouds[c].p, pl.pos)) < 5.5f) pi.hidden = 1;
    int from;
    float dmg = enemies_update(&foes, &world, &pi, dt, &from);
    if (dmg > 0) {
        pl.health -= dmg;
        pl.chain = 0;
        pl.hurt_t = 0.4f;
        pl.invuln = 0.6f;
        cam.shake = 0.3f;
        vec3 away = v3sub(pl.pos, foes.e[from].pos); away.y = 0;
        away = v3norm(away);
        pl.vel = v3scale(away, 6);
        face_point(foes.e[from].pos);
        set_state(PS_LAND);
        anim_play(&pl.anim, ANIM_HIT, 0.04f);
        fx_burst(v3add(pl.pos, v3(0, 1.3f, 0)), 10, 4, rgba32(255, 90, 60, 255));
        if (pl.health <= 0) player_defeated();
    }
    pl.hurt_t = fmaxf(0, pl.hurt_t - dt);
    /* keep Neato from walking through guards */
    bolts_update(dt);
    if (pl.st != PS_DODGE && pl.st != PS_ATTACK) {
        for (int i = 0; i < foes.n; i++) {
            Enemy *e = &foes.e[i];
            if (!enemy_alive(e) || e->st == E_KNOCK) continue;
            vec3 d = v3sub(pl.pos, e->pos);
            if (fabsf(d.y) > 1.2f) continue;
            d.y = 0;
            float l = v3len(d);
            if (l < 0.75f && l > 0.001f) pl.pos = v3add(pl.pos, v3scale(d, (0.75f - l) / l));
        }
    }
    pl.chain_t += dt;
    if (pl.chain_t > 3.0f) pl.chain = 0;
    banner_t = fmaxf(0, banner_t - dt);
}

static void save_game(void) {
    SaveData sd; memset(&sd, 0, sizeof sd);
    memcpy(sd.magic, "NEAT", 4); sd.version = 1;
    sd.mission = mission;
    for (int g = 0; g < foes.ngroups; g++) if (foes.group_left[g] == 0) sd.cleared_mask |= 1 << g;
    sd.best_chain = pl.best_chain; sd.takedowns = pl.takedowns; sd.charge = pl.charge;
    has_save = plat_save_write(&sd, sizeof sd);
}

static void load_game(void) {
    SaveData sd;
    if (!plat_save_read(&sd, sizeof sd) || memcmp(sd.magic, "NEAT", 4)) return;
    mission = sd.mission < 0 ? 0 : (sd.mission > NUM_MISSIONS ? NUM_MISSIONS : sd.mission);
    for (int g = 0; g < foes.ngroups; g++) if (sd.cleared_mask & (1 << g)) enemies_clear_group(&foes, g);
    pl.best_chain = sd.best_chain; pl.takedowns = sd.takedowns; pl.charge = sd.charge;
    if (mission == NUM_MISSIONS - 1 || (mission >= NUM_MISSIONS && !(sd.cleared_mask & (1 << HQ_GROUP))))
        enemies_activate(&foes, HQ_GROUP);
}

static void start_mission(int m) {
    mission = m; mstep = 0; glide_time = 0; slam_done = 0;
    mission_card_t = 4.0f;
    if (m == NUM_MISSIONS - 1) enemies_activate(&foes, HQ_GROUP);
}

/* where the objective marker should point; returns 0 if none */
static int mission_waypoint(vec3 *out) {
    if (mission == 1 && foes.group_left[0]) { *out = foes.group_pos[0]; return 1; }
    if (mission == NUM_MISSIONS - 1) { *out = foes.group_pos[HQ_GROUP]; return 1; }
    if (mission >= 1 && mission < NUM_MISSIONS - 1) {
        float best = 1e9f; int found = 0;
        for (int g = 0; g < NUM_CHECKPOINTS; g++) {
            if (!foes.group_left[g]) continue;
            float d = v3len(v3sub(foes.group_pos[g], pl.pos));
            if (d < best) { best = d; *out = foes.group_pos[g]; found = 1; }
        }
        return found;
    }
    return 0;
}

static int mission_complete(float dt) {
    int cleared = enemies_groups_cleared(&foes);
    switch (mission) {
    case 0:
        if (mstep == 0 && pl.pos.y > 25 && pl.grounded) mstep = 1;
        if (mstep == 1) { if (pl.st == PS_GLIDE) glide_time += dt; if (glide_time > 1.2f) { mstep = 2; slam_done = 0; } }
        return mstep == 2 && slam_done;
    case 1: return foes.group_left[0] == 0;
    case 2: return cleared >= 3;
    case 3:
        for (int g = 0; g < NUM_CHECKPOINTS; g++) if (foes.group_left[g] == 0 && !foes.group_spotted[g]) return 1;
        return cleared >= NUM_CHECKPOINTS;
    case 4: return cleared >= NUM_CHECKPOINTS;
    case 5: return foes.group_left[HQ_GROUP] == 0;
    }
    return 0;
}

static const char *mission_objective(char *buf, int n) {
    int cleared = enemies_groups_cleared(&foes);
    switch (mission) {
    case 0:
        if (mstep == 0) return "Hold R and sprint into a tall building to run up it";
        if (mstep == 1) { snprintf(buf, n, "Jump off and hold X to glide  (%d%%)", (int)(glide_time / 1.2f * 100)); return buf; }
        return "In the air, press O to smoke slam";
    case 1: return "Break the checkpoint south of the plaza";
    case 2: snprintf(buf, n, "Break checkpoints  %d/3", cleared); return buf;
    case 3: return "Clear a checkpoint before anyone spots you";
    case 4: snprintf(buf, n, "Break every checkpoint  %d/%d", cleared, NUM_CHECKPOINTS); return buf;
    case 5: {
        int b = enemies_boss(&foes);
        if (b >= 0 && enemy_alive(&foes.e[b])) { snprintf(buf, n, "Take down the Warden  (%d)", foes.e[b].hp > 0 ? foes.e[b].hp : 0); return buf; }
        return "Clear Vantage HQ";
    }
    }
    return "Free roam: Saltreach Zone 3 is yours";
}

static void mission_update(float dt) {
    mission_card_t = fmaxf(0, mission_card_t - dt);
    if (mission >= NUM_MISSIONS) return;
    if (mission_done_t > 0) {
        mission_done_t -= dt;
        if (mission_done_t <= 0) {
            if (mission + 1 >= NUM_MISSIONS) { mission = NUM_MISSIONS; save_game(); gstate = GS_ENDING; }
            else { start_mission(mission + 1); save_game(); }
        }
        return;
    }
    if (mission_complete(dt)) {
        show_banner("MISSION COMPLETE");
        mission_done_t = 2.5f;
    }
}

void game_update(const Input *in, float dt) {
    if (gstate == GS_TITLE) {
        game_time += dt;
        if (in->pressed & (BTN_UP | BTN_DOWN)) menu_sel = has_save ? !menu_sel : 1;
        if (in->ly < -0.6f && has_save) menu_sel = 0;
        if (in->ly > 0.6f) menu_sel = 1;
        if (in->pressed & BTN_CROSS) {
            if (menu_sel == 0 && has_save) {
                load_game();
                gstate = mission >= NUM_MISSIONS ? GS_PLAY : GS_PLAY;
                if (mission < NUM_MISSIONS) mission_card_t = 4.0f;
            } else { start_mission(0); gstate = GS_INTRO; game_time = 0; }
        }
        return;
    }
    if (gstate == GS_ENDING) {
        game_time += dt;
        if (in->pressed & BTN_CROSS) gstate = GS_PLAY;
        return;
    }
    if (gstate == GS_INTRO) {
        game_time += dt;
        if (in->pressed & (BTN_CROSS | BTN_START)) gstate = GS_PLAY;
        return;
    }
    if (gstate == GS_PAUSE) {
        if (in->pressed & (BTN_START | BTN_CIRCLE)) gstate = GS_PLAY;
        return;
    }
    if (in->pressed & BTN_START) { gstate = GS_PAUSE; return; }
    game_time += dt;
    hint_t += dt;
    pl.scan = approachf(pl.scan, pl.sense ? 1.0f : 0.0f, dt * 5);
    camera_update(in, dt);
    float sim = dt;
    if (hitstop > 0) { hitstop -= dt; sim = dt * 0.08f; }
    player_update(in, sim);
    combat_update(sim);
    mission_update(dt);
    fx_update(dt);
}

/* tall light beams over unbroken checkpoints, visible through the fog */
static void draw_beacons(const mat4 *proj, const mat4 *view) {
    GVert v[MAX_GROUPS * 8]; uint16_t idx[MAX_GROUPS * 12]; int nv = 0, ni = 0;
    vec3 right = v3(view->m[0], 0, view->m[8]);
    right = v3norm(right);
    for (int g = 0; g < foes.ngroups; g++) {
        if (foes.group_left[g] == 0 || !foes.group_active[g] || mission == 0) continue;
        vec3 p = foes.group_pos[g];
        float d = v3len(v3sub(p, pl.pos));
        if (d < 12) continue;
        float w = 0.6f + d * 0.01f;
        uint32_t cb = foes.group_alert[g] ? rgba32(255, 80, 60, 150) : rgba32(255, 160, 50, 130);
        uint32_t ct = cb & 0x00ffffff;
        vec3 r = v3scale(right, w);
        vec3 q[4] = {v3sub(p, r), v3add(p, r), v3add(v3add(p, r), v3(0, 140, 0)), v3add(v3sub(p, r), v3(0, 140, 0))};
        uint32_t cc[4] = {cb, cb, ct, ct};
        uint16_t s = (uint16_t)nv;
        for (int k = 0; k < 4; k++) v[nv++] = (GVert){q[k].x, q[k].y, q[k].z, 0.5f, 0.5f, cc[k]};
        idx[ni++] = s; idx[ni++] = s + 1; idx[ni++] = s + 2; idx[ni++] = s; idx[ni++] = s + 2; idx[ni++] = s + 3;
    }
    if (!ni) return;
    gfx_set_matrices(proj, view);
    gfx_draw(v, nv, idx, ni, world.tex_white, GFX_ADDITIVE | GFX_NO_ZWRITE | GFX_NO_CULL | GFX_NO_FOG);
}

static void draw_minimap(void) {
    const float cx = SCREEN_W - 90, cy = 120, R = 70, scale = R / 70.0f;   /* 70m radius */
    hud_quad(cx - R - 4, cy - R - 4, 2 * R + 8, 2 * R + 8, 0, 0, 0, 0, rgba32(10, 14, 22, 150));
    float c = cosf(cam.yaw), s = sinf(cam.yaw);
    #define MAP(px, pz, ox, oy) do { float dx = (px) - pl.pos.x, dz = (pz) - pl.pos.z; \
        ox = cx - (dx * c - dz * s) * scale; oy = cy - (dx * s + dz * c) * scale; } while (0)
    /* building footprints */
    for (int i = 0; i < world.nbox; i++) {
        const Box *b = &world.box[i];
        if (b->kind != BOX_BUILDING || b->mn.y > 0.5f) continue;
        float mx = (b->mn.x + b->mx.x) * 0.5f, mz = (b->mn.z + b->mx.z) * 0.5f, ox, oy;
        MAP(mx, mz, ox, oy);
        if (fabsf(ox - cx) > R - 6 || fabsf(oy - cy) > R - 6) continue;
        float hw = fmaxf(b->mx.x - b->mn.x, b->mx.z - b->mn.z) * 0.5f * scale;
        hud_quad(ox - hw * 0.7f, oy - hw * 0.7f, hw * 1.4f, hw * 1.4f, 0, 0, 0, 0, rgba32(90, 100, 115, 140));
    }
    for (int i = 0; i < foes.n; i++) {
        const Enemy *e = &foes.e[i];
        if (!enemy_alive(e) || !foes.group_active[e->group]) continue;
        float ox, oy; MAP(e->pos.x, e->pos.z, ox, oy);
        if (fabsf(ox - cx) > R || fabsf(oy - cy) > R) continue;
        uint32_t col = enemy_aware(&foes, i) ? rgba32(255, 70, 60, 255) : rgba32(255, 170, 60, 255);
        hud_quad(ox - 2.5f, oy - 2.5f, 5, 5, 0, 0, 0, 0, col);
    }
    for (int g = 0; g < foes.ngroups; g++) {        /* checkpoint markers clamp to the edge */
        if (foes.group_left[g] == 0 || !foes.group_active[g]) continue;
        float ox, oy; MAP(foes.group_pos[g].x, foes.group_pos[g].z, ox, oy);
        float dx = ox - cx, dy = oy - cy, l = sqrtf(dx * dx + dy * dy);
        if (l > R - 6) { ox = cx + dx / l * (R - 6); oy = cy + dy / l * (R - 6); }
        hud_quad(ox - 4, oy - 4, 8, 8, 0, 0, 0, 0, rgba32(255, 210, 80, 255));
    }
    hud_quad(cx - 3, cy - 5, 6, 10, 0, 0, 0, 0, rgba32(110, 225, 255, 255));    /* player */
    #undef MAP
}

static void hud_center(float y, float scale, uint32_t c, const char *t) {
    float w = 0;
    for (const char *p = t; *p; p++) {
        int k = (unsigned char)*p - 32;
        w += (k >= 0 && k < 96 && font_w[k] ? font_w[k] + 1.5f : 9.0f) * scale;
    }
    hud_text(SCREEN_W / 2 - w / 2, y, scale, c, t);
}

static void draw_hero(const mat4 *proj, const mat4 *view) {
    mat4 W = m4mul(m4translate(pl.pos), m4roty(pl.yaw + pl.spin));
    W = m4mul(W, m4translate(v3(0, pl.pivot, 0)));
    W = m4mul(W, m4mul(m4rotz(pl.bank), m4rotx(pl.body_pitch + pl.anim.out.body_pitch)));
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
    if (gstate != GS_PLAY) return "";
    if (mission == 0) return "";
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
    float fov = 62.0f - 12.0f * pl.aim + clampf((sqrtf(pl.vel.x * pl.vel.x + pl.vel.z * pl.vel.z) - RUN_SPEED) * 0.8f, 0, 10);
    mat4 proj = m4perspective(DEG2RAD(fov), (float)SCREEN_W / SCREEN_H, 0.15f, FOG_END + 40);
    mat4 view = m4lookat(v3add(cam.pos, shake), v3add(cam.target, shake), v3(0, 1, 0));
    gfx_fog(FOG_RGB[0], FOG_RGB[1], FOG_RGB[2], FOG_START, FOG_END);
    world_draw(&world, &proj, &view, cam.pos, cam.fwd, FOG_END);
    enemies_draw(&foes, &proj, &view, cam.pos, FOG_END, pl.scan > 0.5f);
    draw_beacons(&proj, &view);
    if (game_show_player && !(pl.st == PS_DODGE && pl.st_t > 0.03f && pl.st_t < 0.27f)) draw_hero(&proj, &view);
    {   /* smoke bolt heads */
        GVert bv[64]; uint16_t bi[96]; int nb = 0;
        vec3 right = v3(view.m[0], view.m[4], view.m[8]), up = v3(view.m[1], view.m[5], view.m[9]);
        for (int i = 0; i < 16; i++) {
            if (bolts[i].life <= 0) continue;
            vec3 p = bolts[i].p, r = v3scale(right, 0.45f), u = v3scale(up, 0.45f);
            vec3 q[4] = {v3sub(v3sub(p, r), u), v3sub(v3add(p, r), u), v3add(v3add(p, r), u), v3add(v3sub(p, r), u)};
            float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
            for (int k = 0; k < 4; k++) bv[nb * 4 + k] = (GVert){q[k].x, q[k].y, q[k].z, uv[k][0], uv[k][1], MIST_COL};
            uint16_t s0 = (uint16_t)(nb * 4);
            uint16_t *ii = &bi[nb * 6];
            ii[0] = s0; ii[1] = s0 + 1; ii[2] = s0 + 2; ii[3] = s0; ii[4] = s0 + 2; ii[5] = s0 + 3;
            nb++;
        }
        if (nb) { gfx_set_matrices(&proj, &view); gfx_draw(bv, nb * 4, bi, nb * 6, tex_dot, GFX_ADDITIVE | GFX_NO_ZWRITE | GFX_NO_CULL); }
    }
    fx_draw(&proj, &view);

    /* HUD */
    if (!game_show_hud) { gfx_end_frame(); return; }
    gfx_set_matrices(&ortho, &ident);
    if (pl.scan > 0) hud_quad(0, 0, SCREEN_W, SCREEN_H, 0, 0, 0, 0, rgba32(40, 30, 70, (int)(80 * pl.scan)));
    if (pl.hurt_t > 0) hud_quad(0, 0, SCREEN_W, SCREEN_H, 0, 0, 0, 0, rgba32(200, 30, 30, (int)(160 * pl.hurt_t)));
    if (gstate == GS_INTRO || gstate == GS_PAUSE || gstate == GS_TITLE || gstate == GS_ENDING) hud_quad(0, 0, SCREEN_W, SCREEN_H, 0, 0, 0, 0, rgba32(6, 10, 18, 200));
    if (gstate == GS_PLAY || gstate == GS_PAUSE) {
        hud_bar(24, 24, 220, 10, pl.health / 100.0f, rgba32(230, 70, 90, 255));
        hud_bar(24, 42, 220, 8, pl.charge / 100.0f, rgba32(180, 168, 225, 255));
        if (pl.aim > 0.5f && gstate == GS_PLAY) {       /* reticle */
            float cx = SCREEN_W / 2, cy = SCREEN_H / 2;
            uint32_t rc = pl.charge >= BOLT_COST ? rgba32(235, 230, 255, 230) : rgba32(255, 90, 90, 200);
            hud_quad(cx - 1, cy - 12, 2, 8, 0, 0, 0, 0, rc); hud_quad(cx - 1, cy + 4, 2, 8, 0, 0, 0, 0, rc);
            hud_quad(cx - 12, cy - 1, 8, 2, 0, 0, 0, 0, rc); hud_quad(cx + 4, cy - 1, 8, 2, 0, 0, 0, 0, rc);
        }
        draw_minimap();
    }
    if (pl.respawn_fade > 0) hud_quad(0, 0, SCREEN_W, SCREEN_H, 0, 0, 0, 0, rgba32(200, 230, 255, (int)(pl.respawn_fade * 255)));
    hud_flush(world.tex_white);

    char buf[96];
    if (gstate == GS_TITLE) {
        hud_center(120, 3.2f, rgba32(255, 255, 255, 255), "NEATO");
        hud_center(192, 1.2f, rgba32(140, 230, 255, 255), "SALTREACH");
        const char *opts[2] = {"Continue", "New Game"};
        for (int i = 0; i < 2; i++) {
            int on = menu_sel == i;
            uint32_t c = i == 0 && !has_save ? rgba32(120, 120, 130, 160) : on ? rgba32(255, 210, 80, 255) : rgba32(230, 235, 245, 220);
            snprintf(buf, sizeof buf, on ? "> %s <" : "%s", opts[i]);
            hud_center(300 + i * 40, 1.3f, c, buf);
        }
        hud_center(470, 0.8f, rgba32(255, 255, 255, 140), "v1.0 dev  -  X select");
        hud_flush(tex_font);
        gfx_end_frame();
        return;
    }
    if (gstate == GS_ENDING) {
        hud_center(110, 2.0f, rgba32(255, 255, 255, 255), "ZONE 3 IS FREE");
        hud_center(180, 1.0f, rgba32(230, 235, 245, 230), "The Warden is down and Vantage's checkpoints are scrap.");
        hud_center(206, 1.0f, rgba32(230, 235, 245, 230), "In his tablet: shipping logs for the reactor barge,");
        hud_center(232, 1.0f, rgba32(230, 235, 245, 230), "and a second quarantine zone nobody was told about.");
        hud_center(272, 1.0f, rgba32(140, 230, 255, 240), "Neato's story continues.");
        snprintf(buf, sizeof buf, "Best combo x%d     Takedowns %d", pl.best_chain, pl.takedowns);
        hud_center(330, 1.0f, rgba32(255, 210, 80, 240), buf);
        hud_center(400, 0.9f, rgba32(255, 255, 255, 200), "Thanks for playing.  X - keep exploring");
        hud_flush(tex_font);
        gfx_end_frame();
        return;
    }
    if (gstate == GS_INTRO) {
        float a = clampf(game_time * 0.8f, 0, 1);
        uint32_t w = rgba32(255, 255, 255, (int)(255 * a)), cyan = rgba32(140, 230, 255, (int)(255 * a));
        hud_center(110, 2.6f, w, "NEATO");
        hud_center(168, 1.1f, cyan, "SALTREACH  -  QUARANTINE ZONE 3");
        hud_center(225, 1.0f, w, "Six weeks after the flood, Vantage Security sealed the island.");
        hud_center(250, 1.0f, w, "No one gets out. No one asks why.");
        hud_center(285, 1.0f, w, "Neato was on the pier the night the reactor barge burned.");
        hud_center(310, 1.0f, w, "She breathed in the black smoke. Now she can become it.");
        hud_center(350, 1.0f, cyan, "Break the Vantage checkpoints. Find out what they're hiding.");
        if (((int)(game_time * 2)) % 2) hud_center(430, 1.0f, rgba32(255, 255, 255, 200), "Press X to begin");
        hud_flush(tex_font);
        gfx_end_frame();
        return;
    }
    hud_text(252, 18, 1, rgba32(255, 255, 255, 230), "NEATO");
    hud_text(252, 36, 0.8f, rgba32(196, 186, 236, 230), "MIST");
    /* objective */
    if (mission < NUM_MISSIONS) {
        snprintf(buf, sizeof buf, "%d. %s", mission + 1, MISSIONS[mission].title);
        hud_text(24, 62, 0.85f, rgba32(255, 210, 80, 230), buf);
    }
    char ob[96];
    hud_text(24, 80, 0.8f, rgba32(255, 255, 255, 210), mission_objective(ob, sizeof ob));
    vec3 wp;
    if (mission_waypoint(&wp)) {
        snprintf(buf, sizeof buf, "%dm", (int)v3len(v3sub(wp, pl.pos)));
        hud_text(24, 98, 0.75f, rgba32(255, 210, 80, 170), buf);
    }
    if (mission_card_t > 0 && mission < NUM_MISSIONS) {
        float a = clampf(mission_card_t, 0, 1);
        snprintf(buf, sizeof buf, "MISSION %d", mission + 1);
        hud_center(120, 1.0f, rgba32(255, 210, 80, (int)(255 * a)), buf);
        hud_center(142, 2.0f, rgba32(255, 255, 255, (int)(255 * a)), MISSIONS[mission].title);
        hud_center(192, 0.95f, rgba32(220, 230, 240, (int)(230 * a)), MISSIONS[mission].brief);
    }
    if (pl.chain >= 2) {
        snprintf(buf, sizeof buf, "x%d", pl.chain);
        hud_text(SCREEN_W - 120, 210, 2.0f, rgba32(140, 230, 255, 240), buf);
        hud_text(SCREEN_W - 118, 246, 0.8f, rgba32(255, 255, 255, 200), "COMBO");
    }
    if (enemies_counterable(&foes, pl.pos, 5.0f) >= 0 && ((int)(game_time * 12)) % 2)
        hud_center(SCREEN_H - 150, 1.3f, rgba32(255, 255, 255, 255), "/\\  COUNTER");
    if (banner_t > 0) {
        float a = clampf(banner_t, 0, 1);
        hud_center(mission_card_t > 0 ? 240 : 150, 1.8f, rgba32(255, 255, 255, (int)(255 * a)), banner);
    }
    const char *sn = state_name();
    if (*sn) hud_center(SCREEN_H - 60, 1, rgba32(150, 230, 255, 220), sn);
    if (pl.scan > 0.5f && gstate == GS_PLAY) hud_center(SCREEN_H - 84, 0.9f, rgba32(196, 186, 236, 220), "MIST SENSE");
    if (hint_t < 14 && gstate == GS_PLAY) {
        uint32_t c = rgba32(255, 255, 255, (int)(230 * clampf(14 - hint_t, 0, 1)));
        hud_text(24, SCREEN_H - 155, 0.85f, c, "X jump / hold in air: mist glide");
        hud_text(24, SCREEN_H - 138, 0.85f, c, "R sprint - into a wall: wall run");
        hud_text(24, SCREEN_H - 121, 0.85f, c, "O mist dash / in air: smoke slam");
        hud_text(24, SCREEN_H - 104, 0.85f, c, "[] strike   /\\ counter or smoke bomb");
        hud_text(24, SCREEN_H - 87, 0.85f, c, "hold L aim, R fire smoke bolts");
        hud_text(24, SCREEN_H - 70, 0.85f, c, "Up: mist sense (see guards through walls)");
    }
    if (gstate == GS_PAUSE) {
        hud_center(120, 2.0f, rgba32(255, 255, 255, 255), "PAUSED");
        static const char *lines[] = {
            "Left stick - move        Right stick - camera",
            "X - jump, hold in the air to mist glide, X on a wall to kick off",
            "R - sprint into a building to run up it.  Hold L to aim, R fires",
            "[] - strike (chains between guards)   O - mist dash / smoke slam",
            "/\\ - counter when a guard flashes, otherwise smoke bomb",
            "Up - mist sense    Behind, above or in smoke: silent takedown",
            "",
            "Start / O - resume       Start + Select - quit",
        };
        for (int i = 0; i < 8; i++) hud_center(190 + i * 26, 0.95f, rgba32(220, 230, 240, 230), lines[i]);
        snprintf(buf, sizeof buf, "Best combo x%d     Takedowns %d", pl.best_chain, pl.takedowns);
        hud_center(420, 0.9f, rgba32(140, 230, 255, 230), buf);
    }
    fps_frames++;
    snprintf(buf, sizeof buf, "%d FPS", fps_show);
    hud_text(SCREEN_W - 70, SCREEN_H - 24, 0.7f, rgba32(255, 255, 255, 120), buf);
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
    d->health = pl.health; d->cleared = enemies_groups_cleared(&foes); d->chain = pl.best_chain;
    d->guards_down = 0; for (int i = 0; i < foes.n; i++) if (!enemy_alive(&foes.e[i])) d->guards_down++;
}
