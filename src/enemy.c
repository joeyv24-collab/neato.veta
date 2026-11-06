#include <stdlib.h>
#include <string.h>
#include "enemy.h"
#include "gfx.h"

#define GUARD_R      0.42f
#define CHASE_SPEED  5.0f
#define WALK_SPEED   1.6f
#define WINDUP_TIME  0.7f
#define SIGHT_RANGE  17.0f
#define LEASH        48.0f
#define DAMAGE       12.0f

static unsigned erng = 4242;

/* checkpoints sit on road segments / intersections around the district */
static const float CHECKPOINTS[][2] = {
    {6, -18}, {-66, -114}, {78, -66}, {-114, 30}, {78, 126}, {-18, 78},
};

int enemy_alive(const Enemy *e) { return e->st != E_DOWN && !(e->st == E_KNOCK && e->hp <= 0); }
int enemy_aware(const Enemies *es, int i) { return es->group_alert[es->e[i].group]; }

static int count_group(const Enemies *es, int g) {
    int n = 0;
    for (int i = 0; i < es->n; i++) if (es->e[i].group == g && enemy_alive(&es->e[i])) n++;
    return n;
}

static void spawn_guard(Enemies *es, vec3 p, int group, int heavy) {
    if (es->n >= MAX_ENEMIES) return;
    Enemy *e = &es->e[es->n++];
    memset(e, 0, sizeof *e);
    e->pos = e->home = p;
    e->group = group;
    e->heavy = heavy;
    e->hp = heavy ? 6 : 3;
    e->yaw = rng_range(&erng, -3.14f, 3.14f);
    e->st = E_PATROL;
    e->look_t = rng_range(&erng, 0, 3);
    e->cooldown = rng_range(&erng, 0.5f, 2.0f);
    e->circle_ang = rng_range(&erng, 0, 6.28f);
}

void enemies_init(Enemies *es, World *w, unsigned seed) {
    memset(es, 0, sizeof *es);
    erng = seed ? seed : 4242;
    es->ngroups = (int)(sizeof CHECKPOINTS / sizeof CHECKPOINTS[0]);
    if (es->ngroups > MAX_GROUPS) es->ngroups = MAX_GROUPS;
    vec3 pos[MAX_GROUPS];
    for (int g = 0; g < es->ngroups; g++) {
        pos[g] = v3(CHECKPOINTS[g][0], 0, CHECKPOINTS[g][1]);
        es->group_pos[g] = pos[g];
        int count = 3 + g / 2;
        for (int k = 0; k < count; k++) {
            float a = k / (float)count * 6.28f + g;
            spawn_guard(es, v3(pos[g].x + cosf(a) * 3.0f, 0, pos[g].z + sinf(a) * 3.0f), g, g >= 2 && k == 0);
        }
    }
    world_add_checkpoints(w, pos, es->ngroups);
    static const uint8_t white[4] = {255, 255, 255, 255};
    es->tex_white = gfx_texture(1, 1, white, 0, 1);
    enum { S = 32 };
    static uint8_t dot[S * S * 4];
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
        float dx = (x + 0.5f) / S * 2 - 1, dy = (y + 0.5f) / S * 2 - 1;
        float a = clampf(1 - sqrtf(dx * dx + dy * dy), 0, 1);
        uint8_t *p = &dot[(y * S + x) * 4];
        p[0] = p[1] = p[2] = 255; p[3] = (uint8_t)(a * a * 255);
    }
    es->tex_dot = gfx_texture(S, S, dot, 1, 0);
    for (int g = 0; g < es->ngroups; g++) es->group_left[g] = count_group(es, g);
}

void enemies_reset(Enemies *es) {
    for (int g = 0; g < es->ngroups; g++) es->group_alert[g] = 0;
    for (int i = 0; i < es->n; i++) {
        Enemy *e = &es->e[i];
        if (!enemy_alive(e)) continue;
        e->pos = e->home; e->vel = v3(0, 0, 0);
        e->st = E_PATROL; e->t = 0;
        e->hp = e->heavy ? 6 : 3;
    }
}

void enemies_alert_group(Enemies *es, int g) {
    if (es->group_alert[g]) return;
    es->group_alert[g] = 1;
    for (int i = 0; i < es->n; i++) {
        Enemy *e = &es->e[i];
        if (e->group == g && e->st == E_PATROL) { e->st = E_CHASE; e->t = 0; }
    }
}

int enemies_groups_cleared(const Enemies *es) {
    int n = 0;
    for (int g = 0; g < es->ngroups; g++) if (es->group_left[g] == 0) n++;
    return n;
}

int enemies_attackers(const Enemies *es) {
    int n = 0;
    for (int i = 0; i < es->n; i++) if (es->e[i].st == E_WINDUP || es->e[i].st == E_ATTACK) n++;
    return n;
}

static void face(Enemy *e, vec3 to, float rate, float dt) {
    vec3 d = v3sub(to, e->pos);
    if (d.x * d.x + d.z * d.z < 1e-4f) return;
    float diff = wrap_angle(atan2f(d.x, d.z) - e->yaw);
    float s = rate * dt;
    e->yaw = wrap_angle(e->yaw + clampf(diff, -s, s));
}

static void move_towards(Enemy *e, vec3 target, float speed, float dt) {
    vec3 d = v3sub(target, e->pos); d.y = 0;
    float l = v3len(d);
    vec3 want = l > 0.05f ? v3scale(d, speed / l) : v3(0, 0, 0);
    e->vel.x = approachf(e->vel.x, want.x, 20 * dt);
    e->vel.z = approachf(e->vel.z, want.z, 20 * dt);
}

int enemy_hit(Enemies *es, int i, int dmg, vec3 from, float knock, float lift) {
    Enemy *e = &es->e[i];
    if (!enemy_alive(e)) return 0;
    e->hp -= dmg;
    e->flash = 0.18f;
    enemies_alert_group(es, e->group);
    vec3 away = v3sub(e->pos, from); away.y = 0;
    away = v3len(away) > 0.01f ? v3norm(away) : v3(sinf(e->yaw + 3.14f), 0, cosf(e->yaw + 3.14f));
    e->yaw = atan2f(-away.x, -away.z);      /* face the attacker */
    if (e->hp <= 0 || knock > 7 || lift > 3) {
        if (lift < 4) lift = 4;
        e->knock = v3scale(away, fmaxf(knock, 5));
        e->knock_y = lift;
        e->st = E_KNOCK; e->t = 0;
        if (e->hp <= 0) {
            es->group_left[e->group] = count_group(es, e->group);
            return 1;
        }
    } else {
        e->knock = v3scale(away, knock);
        e->st = E_STAGGER; e->t = 0;
    }
    return 0;
}

static int can_see(const World *w, const Enemy *e, vec3 p) {
    vec3 eye = v3add(e->pos, v3(0, 1.6f, 0)), tgt = v3add(p, v3(0, 1.2f, 0));
    float t;
    return !world_raycast(w, eye, tgt, &t, NULL);
}

float enemies_update(Enemies *es, const World *w, const PlayerInfo *pi, float dt, int *hit_from) {
    float damage = 0;
    *hit_from = -1;
    int attackers = enemies_attackers(es);
    for (int i = 0; i < es->n; i++) {
        Enemy *e = &es->e[i];
        e->t += dt;
        e->flash = fmaxf(0, e->flash - dt);
        if (e->st == E_DOWN) continue;
        vec3 to_p = v3sub(pi->pos, e->pos); to_p.y = 0;
        float dist = v3len(to_p);
        float home_d = v3len(v3sub(pi->pos, es->group_pos[e->group]));

        switch (e->st) {
        case E_PATROL: {
            e->look_t -= dt;
            if (e->look_t < 0) {
                e->look_t = rng_range(&erng, 2.5f, 5.0f);
                e->circle_ang = rng_range(&erng, 0, 6.28f);
            }
            vec3 wp = v3add(e->home, v3(cosf(e->circle_ang) * 4, 0, sinf(e->circle_ang) * 4));
            if (v3len(v3sub(wp, e->pos)) > 0.6f) { move_towards(e, wp, WALK_SPEED, dt); face(e, wp, 3, dt); }
            else move_towards(e, e->pos, 0, dt);
            /* spotting: a cone in front, or very close; harder from above */
            float range = pi->hidden ? 7.0f : SIGHT_RANGE;
            float dy = pi->pos.y - e->pos.y;
            if (dist < range && fabsf(dy) < (pi->hidden ? 4.0f : 10.0f)) {
                vec3 f = v3(sinf(e->yaw), 0, cosf(e->yaw));
                float c = dist > 0.01f ? v3dot(f, v3scale(to_p, 1 / dist)) : 1;
                if ((c > 0.4f || dist < 1.2f) && can_see(w, e, pi->pos)) enemies_alert_group(es, e->group);
            }
        } break;
        case E_CHASE: case E_CIRCLE: {
            if (home_d > LEASH) {            /* give up: walk back */
                es->group_alert[e->group] = 0;
                e->st = E_PATROL; e->t = 0;
                break;
            }
            face(e, pi->pos, 8, dt);
            if (e->st == E_CHASE) {
                move_towards(e, pi->pos, CHASE_SPEED, dt);
                if (dist < 4.5f) { e->st = E_CIRCLE; e->t = 0; }
            } else {
                e->circle_ang += dt * 0.35f * ((i & 1) ? 1 : -1);
                vec3 slot = v3add(pi->pos, v3(cosf(e->circle_ang) * 4.0f, 0, sinf(e->circle_ang) * 4.0f));
                move_towards(e, slot, 2.4f, dt);
                if (dist > 7) { e->st = E_CHASE; e->t = 0; }
                e->cooldown -= dt;
                int max_att = count_group(es, e->group) > 3 ? 2 : 1;
                if (e->cooldown <= 0 && attackers < max_att && fabsf(pi->pos.y - e->pos.y) < 1.5f) {
                    e->st = E_WINDUP; e->t = 0; attackers++;
                }
            }
        } break;
        case E_WINDUP:
            face(e, pi->pos, 10, dt);
            if (dist > 1.7f) move_towards(e, pi->pos, 4.0f, dt); else move_towards(e, e->pos, 0, dt);
            if (e->t > WINDUP_TIME) { e->st = E_ATTACK; e->t = 0; }
            break;
        case E_ATTACK: {
            vec3 f = v3(sinf(e->yaw), 0, cosf(e->yaw));
            float k = e->t < 0.15f ? 6.0f : 0;
            e->vel.x = f.x * k; e->vel.z = f.z * k;
            if (e->t >= 0.14f && e->t - dt < 0.14f && dist < 2.1f && !pi->invulnerable && fabsf(pi->pos.y - e->pos.y) < 1.5f) {
                damage += e->heavy ? DAMAGE * 1.5f : DAMAGE;
                *hit_from = i;
            }
            if (e->t > 0.4f) { e->st = E_RECOVER; e->t = 0; }
        } break;
        case E_RECOVER:
            move_towards(e, e->pos, 0, dt);
            if (e->t > 0.5f) { e->st = E_CIRCLE; e->t = 0; e->cooldown = rng_range(&erng, 1.6f, 3.6f); }
            break;
        case E_STAGGER:
            e->vel.x = e->knock.x * (1 - e->t / 0.45f); e->vel.z = e->knock.z * (1 - e->t / 0.45f);
            if (e->t > 0.45f) { e->st = E_CIRCLE; e->t = 0; e->cooldown = rng_range(&erng, 0.8f, 2.0f); }
            break;
        case E_KNOCK: {
            e->vel.x = e->knock.x; e->vel.z = e->knock.z;
            e->knock_y -= 22 * dt;
            e->pos.y += e->knock_y * dt;
            e->roll += dt * 6;
            float fl = world_floor(w, e->pos.x, e->pos.z, e->pos.y + 0.2f, 0.2f);
            if (fl < -100) fl = WATER_Y;
            if (e->pos.y <= fl && e->knock_y < 0) {
                e->pos.y = fl;
                e->knock = v3scale(e->knock, 0.3f);
                if (e->hp <= 0) { e->st = E_DOWN; e->t = 0; e->vel = v3(0, 0, 0); }
                else { e->st = E_RECOVER; e->t = -0.6f; e->knock_y = 0; e->roll = 0; }
            }
        } break;
        case E_DOWN: break;
        }

        /* integrate */
        e->pos.x += e->vel.x * dt;
        e->pos.z += e->vel.z * dt;
        if (e->st != E_KNOCK && e->st != E_DOWN) {
            world_push_circle(w, &e->pos, GUARD_R, e->pos.y + 0.3f, e->pos.y + 1.8f);
            e->pos.y = 0;
            /* don't stand inside the player */
            vec3 d = v3sub(e->pos, pi->pos); d.y = 0;
            float l = v3len(d);
            if (l < 0.8f && l > 0.001f && fabsf(pi->pos.y - e->pos.y) < 1.5f)
                e->pos = v3add(e->pos, v3scale(d, (0.8f - l) / l));
        }
        float hs = sqrtf(e->vel.x * e->vel.x + e->vel.z * e->vel.z);
        e->phase += dt * hs / 1.6f * 6.2832f;
    }
    /* keep guards apart */
    for (int i = 0; i < es->n; i++) for (int j = i + 1; j < es->n; j++) {
        Enemy *a = &es->e[i], *b = &es->e[j];
        if (!enemy_alive(a) || !enemy_alive(b) || a->st == E_KNOCK || b->st == E_KNOCK) continue;
        vec3 d = v3sub(a->pos, b->pos); d.y = 0;
        float l = v3len(d);
        if (l < 0.9f && l > 0.001f) {
            vec3 push = v3scale(d, (0.9f - l) / l * 0.5f);
            a->pos = v3add(a->pos, push); b->pos = v3sub(b->pos, push);
        }
    }
    return damage;
}

int enemies_target(const Enemies *es, vec3 from, vec3 dir, float range) {
    int best = -1; float bs = 1e9f;
    float dl = v3len(dir);
    for (int i = 0; i < es->n; i++) {
        const Enemy *e = &es->e[i];
        if (!enemy_alive(e) || e->st == E_KNOCK) continue;
        vec3 d = v3sub(e->pos, from);
        if (fabsf(d.y) > 3) continue;
        d.y = 0;
        float l = v3len(d);
        if (l > range) continue;
        float s = l;
        if (dl > 0.1f && l > 0.01f) s -= 5.0f * v3dot(v3scale(d, 1 / l), v3scale(dir, 1 / dl));
        if (s < bs) { bs = s; best = i; }
    }
    return best;
}

int enemies_counterable(const Enemies *es, vec3 from, float range) {
    int best = -1; float bd = range;
    for (int i = 0; i < es->n; i++) {
        const Enemy *e = &es->e[i];
        int open = (e->st == E_WINDUP && e->t > 0.15f) || (e->st == E_ATTACK && e->t < 0.14f);
        if (!open) continue;
        float d = v3len(v3sub(e->pos, from));
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

/* ------------------------------------------------------------------ */
/* rendering: each guard is ~13 boxes posed in code                     */

#define MAXV 24000
static GVert gv[MAXV];
static uint16_t gi[MAXV / 4 * 6];
static int gnv, gni;
static uint32_t override_col; static int use_override;

static uint32_t mulc(uint32_t c, float k) {
    int r = (int)((c & 255) * k), g = (int)(((c >> 8) & 255) * k), b = (int)(((c >> 16) & 255) * k);
    return rgba32(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b, (c >> 24) & 255);
}

static void emit_box(mat4 m, vec3 c, vec3 h, uint32_t col, int emissive) {
    if (gnv + 24 > MAXV) return;
    static const float N[6][3] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    static const float SH[6] = {0.95f, 0.6f, 0.82f, 0.7f, 1.0f, 0.45f};
    for (int f = 0; f < 6; f++) {
        vec3 n = v3(N[f][0], N[f][1], N[f][2]);
        vec3 u = fabsf(n.y) > 0.5f ? v3(1, 0, 0) : v3(0, 1, 0);
        vec3 r = v3cross(u, n);                 /* so that r x u = n (CCW from outside) */
        vec3 corners[4];
        float sr[4] = {-1, 1, 1, -1}, su[4] = {-1, -1, 1, 1};
        for (int k = 0; k < 4; k++) {
            vec3 p = v3(c.x + (n.x + r.x * sr[k] + u.x * su[k]) * h.x,
                        c.y + (n.y + r.y * sr[k] + u.y * su[k]) * h.y,
                        c.z + (n.z + r.z * sr[k] + u.z * su[k]) * h.z);
            corners[k] = m4point(m, p);
        }
        uint32_t cc = use_override ? override_col : (emissive ? col : mulc(col, SH[f]));
        uint16_t s = (uint16_t)gnv;
        for (int k = 0; k < 4; k++) gv[gnv++] = (GVert){corners[k].x, corners[k].y, corners[k].z, 0.5f, 0.5f, cc};
        gi[gni++] = s; gi[gni++] = s + 1; gi[gni++] = s + 2; gi[gni++] = s; gi[gni++] = s + 2; gi[gni++] = s + 3;
    }
}

typedef struct { float thigh[2], knee[2], arm[2], raise[2], elbow[2], lean, twist, pitch, bob; } GPose;

static void guard_pose(const Enemy *e, GPose *p) {
    memset(p, 0, sizeof *p);
    float s = sinf(e->phase), c = cosf(e->phase);
    float hs = sqrtf(e->vel.x * e->vel.x + e->vel.z * e->vel.z);
    float amp = clampf(hs / 5.0f, 0, 1);
    p->thigh[0] = 0.7f * s * amp; p->thigh[1] = -0.7f * s * amp;
    p->knee[0] = (0.2f + 0.9f * (0.5f - 0.5f * c)) * amp; p->knee[1] = (0.2f + 0.9f * (0.5f + 0.5f * c)) * amp;
    p->arm[0] = -0.6f * s * amp; p->arm[1] = 0.6f * s * amp;
    p->elbow[0] = p->elbow[1] = 0.3f + 0.8f * amp;
    p->raise[0] = p->raise[1] = 0.12f;
    p->lean = 0.15f * amp;
    p->bob = -0.03f * amp * fabsf(c);
    switch (e->st) {
    case E_CIRCLE: case E_CHASE:
        p->arm[1] = 0.5f; p->elbow[1] = 1.2f;       /* baton ready */
        break;
    case E_WINDUP: {
        float k = smooth01(e->t / 0.3f);
        p->arm[1] = -2.6f * k; p->elbow[1] = 0.9f * k; p->raise[1] = 0.3f;
        p->twist = -0.5f * k; p->lean = -0.1f;
        p->thigh[0] = 0.35f; p->knee[0] = 0.3f; p->thigh[1] = -0.3f; p->knee[1] = 0.3f;
    } break;
    case E_ATTACK: {
        float k = smooth01(e->t / 0.14f);
        p->arm[1] = -2.6f + 4.0f * k; p->elbow[1] = 0.9f * (1 - k) + 0.1f;
        p->twist = -0.5f + 1.0f * k; p->lean = 0.35f * k;
        p->thigh[0] = 0.5f; p->knee[0] = 0.4f; p->thigh[1] = -0.4f; p->knee[1] = 0.3f;
    } break;
    case E_STAGGER: case E_RECOVER: {
        float k = e->st == E_STAGGER ? 1.0f - smooth01(e->t / 0.45f) : 0.4f * (1.0f - smooth01(e->t / 0.5f));
        p->lean = -0.6f * k; p->raise[0] = p->raise[1] = 0.12f + 0.9f * k;
        p->arm[0] = p->arm[1] = -0.3f * k;
        p->knee[0] = p->knee[1] = 0.5f * k;
    } break;
    case E_KNOCK:
        p->pitch = -clampf(e->t * 4, 0, 1.45f);
        p->raise[0] = p->raise[1] = 1.2f; p->thigh[0] = 0.8f; p->knee[0] = 1.0f; p->thigh[1] = 0.3f;
        break;
    case E_DOWN:
        p->pitch = -1.52f;
        p->raise[0] = 0.9f; p->raise[1] = 0.3f; p->thigh[0] = 0.25f; p->knee[0] = 0.5f;
        break;
    default: break;
    }
}

static void build_guard(const Enemy *e) {
    GPose p; guard_pose(e, &p);
    float sc = e->heavy ? 1.18f : 1.0f;
    uint32_t armor = e->heavy ? rgba32(70, 60, 58, 255) : rgba32(52, 62, 82, 255);
    uint32_t suit = rgba32(38, 40, 46, 255), pad = rgba32(120, 128, 140, 255);
    uint32_t visor = rgba32(255, 150, 40, 255);
    if (e->flash > 0) armor = suit = pad = rgba32(255, 255, 255, 255);
    if (e->st == E_DOWN) visor = rgba32(60, 30, 20, 255);
    mat4 root = m4mul(m4translate(v3add(e->pos, v3(0, p.bob, 0))), m4roty(e->yaw));
    root = m4mul(root, m4mul(m4rotx(p.pitch), m4scale(sc)));
    if (e->st == E_DOWN) root = m4mul(root, m4translate(v3(0, 0.12f, 0)));
    /* legs */
    for (int s = 0; s < 2; s++) {
        float x = s ? -0.12f : 0.12f;
        mat4 hip = m4mul(root, m4mul(m4translate(v3(x, 0.95f, 0)), m4rotx(-p.thigh[s])));
        emit_box(hip, v3(0, -0.23f, 0), v3(0.09f, 0.24f, 0.1f), suit, 0);
        emit_box(hip, v3(0, -0.15f, 0.02f), v3(0.1f, 0.1f, 0.1f), armor, 0);
        mat4 knee = m4mul(hip, m4mul(m4translate(v3(0, -0.46f, 0)), m4rotx(p.knee[s])));
        emit_box(knee, v3(0, -0.22f, 0), v3(0.08f, 0.23f, 0.09f), suit, 0);
        emit_box(knee, v3(0, -0.06f, 0.05f), v3(0.085f, 0.08f, 0.06f), pad, 0);
        emit_box(knee, v3(0, -0.46f, 0.05f), v3(0.08f, 0.04f, 0.14f), rgba32(25, 25, 28, 255), 0);
    }
    /* torso */
    mat4 torso = m4mul(root, m4mul(m4translate(v3(0, 1.0f, 0)), m4mul(m4roty(p.twist), m4rotx(p.lean))));
    emit_box(root, v3(0, 0.98f, 0), v3(0.2f, 0.1f, 0.12f), suit, 0);
    emit_box(torso, v3(0, 0.28f, 0), v3(0.24f, 0.28f, 0.15f), armor, 0);
    emit_box(torso, v3(0, 0.32f, 0.11f), v3(0.18f, 0.16f, 0.06f), pad, 0);     /* chest plate */
    /* head + helmet + visor */
    mat4 head = m4mul(torso, m4translate(v3(0, 0.58f, 0)));
    emit_box(head, v3(0, 0.15f, 0), v3(0.14f, 0.15f, 0.15f), armor, 0);
    emit_box(head, v3(0, 0.16f, 0.14f), v3(0.11f, 0.035f, 0.025f), visor, 1);
    /* arms */
    for (int s = 0; s < 2; s++) {
        float x = s ? -0.31f : 0.31f, sg = s ? -1.0f : 1.0f;
        mat4 sh = m4mul(torso, m4mul(m4translate(v3(x, 0.48f, 0)), m4mul(m4rotx(-p.arm[s]), m4rotz(p.raise[s] * sg))));
        emit_box(sh, v3(0, 0.0f, 0), v3(0.11f, 0.08f, 0.11f), pad, 0);
        emit_box(sh, v3(0, -0.16f, 0), v3(0.065f, 0.16f, 0.065f), suit, 0);
        mat4 el = m4mul(sh, m4mul(m4translate(v3(0, -0.32f, 0)), m4rotx(-p.elbow[s])));
        emit_box(el, v3(0, -0.14f, 0), v3(0.06f, 0.15f, 0.06f), armor, 0);
        if (s == 1 && e->st != E_DOWN) {     /* shock baton */
            mat4 hand = m4mul(el, m4translate(v3(0, -0.3f, 0)));
            emit_box(hand, v3(0, 0, 0.22f), v3(0.025f, 0.025f, 0.26f), rgba32(30, 30, 34, 255), 0);
            emit_box(hand, v3(0, 0, 0.47f), v3(0.035f, 0.035f, 0.035f), rgba32(255, 170, 60, 255), 1);
        }
    }
}

void enemies_draw(Enemies *es, const mat4 *proj, const mat4 *view, vec3 cam, float range, int xray) {
    gfx_set_matrices(proj, view);
    for (int pass = 0; pass < (xray ? 2 : 1); pass++) {
        gnv = gni = 0;
        use_override = pass == 1;
        for (int i = 0; i < es->n; i++) {
            Enemy *e = &es->e[i];
            if (v3len(v3sub(e->pos, cam)) > range) continue;
            if (pass == 1 && !enemy_alive(e)) continue;
            override_col = enemy_aware(es, i) ? rgba32(255, 70, 50, 120) : rgba32(255, 170, 60, 120);
            build_guard(e);
            if (gnv > MAXV - 600) {
                gfx_draw(gv, gnv, gi, gni, es->tex_white, pass ? GFX_BLEND | GFX_NO_ZTEST | GFX_NO_ZWRITE | GFX_NO_FOG : 0);
                gnv = gni = 0;
            }
        }
        if (gni) gfx_draw(gv, gnv, gi, gni, es->tex_white, pass ? GFX_BLEND | GFX_NO_ZTEST | GFX_NO_ZWRITE | GFX_NO_FOG : 0);
    }
    use_override = 0;

    /* wind-up warning above heads: the counter cue */
    vec3 right = v3(view->m[0], view->m[4], view->m[8]), up = v3(view->m[1], view->m[5], view->m[9]);
    gnv = gni = 0;
    for (int i = 0; i < es->n; i++) {
        Enemy *e = &es->e[i];
        if (e->st != E_WINDUP) continue;
        float pulse = 0.35f + 0.15f * sinf(e->t * 30);
        uint32_t c = e->t > 0.15f ? rgba32(255, 255, 255, 255) : rgba32(255, 160, 40, 255);
        vec3 p = v3add(e->pos, v3(0, 2.25f * (e->heavy ? 1.18f : 1), 0));
        vec3 r = v3scale(right, pulse), u = v3scale(up, pulse);
        vec3 q[4] = {v3sub(v3sub(p, r), u), v3sub(v3add(p, r), u), v3add(v3add(p, r), u), v3add(v3sub(p, r), u)};
        uint16_t s = (uint16_t)gnv;
        float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
        for (int k = 0; k < 4; k++) gv[gnv++] = (GVert){q[k].x, q[k].y, q[k].z, uv[k][0], uv[k][1], c};
        gi[gni++] = s; gi[gni++] = s + 1; gi[gni++] = s + 2; gi[gni++] = s; gi[gni++] = s + 2; gi[gni++] = s + 3;
    }
    if (gni) gfx_draw(gv, gnv, gi, gni, es->tex_dot, GFX_ADDITIVE | GFX_NO_ZWRITE | GFX_NO_ZTEST | GFX_NO_CULL | GFX_NO_FOG);
}

/* silent stealth takedown: the guard drops without alerting the group */
void enemy_takedown(Enemies *es, int i) {
    Enemy *e = &es->e[i];
    e->hp = 0;
    e->flash = 0.1f;
    e->st = E_KNOCK; e->t = 0;
    e->knock = v3(0, 0, 0); e->knock_y = 2.5f;
    es->group_left[e->group] = count_group(es, e->group);
}
