#include <stdio.h>
#include <string.h>
#include "anim.h"

void anim_rig(Rig *r, const Model *m) {
    static const char *sp[7] = {"DEF-spine", "DEF-spine.001", "DEF-spine.002", "DEF-spine.003",
                                "DEF-spine.004", "DEF-spine.005", "DEF-spine.006"};
    for (int i = 0; i < 7; i++) r->spine[i] = model_bone(m, sp[i]);
    const char *side[2] = {"L", "R"};
    char buf[48];
    for (int s = 0; s < 2; s++) {
#define B(field, fmt) snprintf(buf, sizeof buf, fmt, side[s]); r->field[s] = model_bone(m, buf);
        B(thigh, "DEF-thigh.%s") B(shin, "DEF-shin.%s") B(foot, "DEF-foot.%s")
        B(upper, "DEF-upper_arm.%s") B(fore, "DEF-forearm.%s") B(shoulder, "DEF-shoulder.%s")
        B(hand, "DEF-hand.%s")
#undef B
    }
    r->bones_ok = r->spine[0] >= 0 && r->thigh[0] >= 0 && r->upper[0] >= 0;
}

void anim_init(Animator *a) {
    memset(a, 0, sizeof *a);
    a->blend = 1;
}

void anim_play(Animator *a, AnimId id, float blend_time) {
    if (a->cur == id) return;
    a->prev = a->out;
    a->cur = id;
    a->t = 0;
    a->blend = blend_time > 0 ? 0 : 1;
    a->blend_time = blend_time;
}

/* resting arm angle: the model is in an A-pose, bring arms closer to the body */
#define ARM_REST (-0.55f)

static void base(AnimParams *p) {
    memset(p, 0, sizeof *p);
    p->raise_l = p->raise_r = ARM_REST;
    p->fore_l = p->fore_r = 0.15f;
}

static void pose_of(const Animator *a, AnimParams *p) {
    float t = a->t, ph = a->phase;
    float s = sinf(ph), c = cosf(ph);
    base(p);
    switch (a->cur) {
    case ANIM_IDLE: {
        float br = sinf(t * 2.2f);
        p->chest = 0.03f * br;
        p->head = -0.02f * br;
        p->raise_l += 0.03f * br; p->raise_r += 0.03f * br;
        p->fore_l = p->fore_r = 0.25f;
        p->spread_l = p->spread_r = 0.05f;
        p->bob = -0.005f + 0.004f * br;
        /* shift weight and glance around now and then */
        p->hip_side = 0.04f + 0.03f * sinf(t * 0.5f);
        float look = sinf(t * 0.45f) * sinf(t * 0.23f);
        p->twist = 0.22f * look;
        p->thigh_l = 0.04f * sinf(t * 0.5f); p->shin_l = 0.08f + 0.06f * sinf(t * 0.5f);
    } break;
    case ANIM_RUN: {
        p->lean = 0.12f;
        p->thigh_l = 0.55f * s;  p->thigh_r = -0.55f * s;
        p->shin_l = 0.25f + 0.65f * (0.5f - 0.5f * c) * (s < 0 ? 1.4f : 0.6f);
        p->shin_r = 0.25f + 0.65f * (0.5f + 0.5f * c) * (s > 0 ? 1.4f : 0.6f);
        p->arm_l = -0.55f * s;  p->arm_r = 0.55f * s;
        p->fore_l = 1.1f + 0.25f * s; p->fore_r = 1.1f - 0.25f * s;
        p->raise_l = p->raise_r = ARM_REST + 0.18f;
        p->foot_l = 0.3f * (s > 0 ? s : 0); p->foot_r = 0.3f * (s < 0 ? -s : 0);
        p->twist = 0.12f * s;
        p->bob = 0.04f * fabsf(c) - 0.03f;
    } break;
    case ANIM_SPRINT: {
        p->lean = 0.35f; p->head = -0.25f;
        p->thigh_l = 0.85f * s;  p->thigh_r = -0.85f * s;
        p->shin_l = 0.3f + 1.2f * (0.5f - 0.5f * c) * (s < 0 ? 1.3f : 0.5f);
        p->shin_r = 0.3f + 1.2f * (0.5f + 0.5f * c) * (s > 0 ? 1.3f : 0.5f);
        p->arm_l = -0.85f * s; p->arm_r = 0.85f * s;
        p->fore_l = p->fore_r = 1.5f;
        p->raise_l = p->raise_r = ARM_REST + 0.2f;
        p->twist = 0.18f * s;
        p->bob = 0.06f * fabsf(c) - 0.06f;
    } break;
    case ANIM_JUMP: {
        if (t < 0.07f) {                       /* quick crouch as she pushes off */
            float c = sinf(t / 0.07f * 3.1416f);
            p->thigh_l = p->thigh_r = 0.6f * c; p->shin_l = p->shin_r = 1.0f * c;
            p->foot_l = p->foot_r = -0.4f * c; p->lean = 0.3f * c; p->bob = -0.15f * c;
            p->arm_l = p->arm_r = -0.5f * c;
            break;
        }
        float k = smooth01((t - 0.07f) / 0.22f);
        p->lean = 0.1f;
        p->thigh_l = 0.9f * k; p->shin_l = 1.4f * k;
        p->thigh_r = -0.2f * k; p->shin_r = 0.6f * k;
        p->arm_l = -0.6f * k; p->arm_r = 0.9f * k;
        p->raise_l = p->raise_r = ARM_REST + 0.5f * k;
        p->fore_l = p->fore_r = 0.8f;
    } break;
    case ANIM_FALL: {
        float w = sinf(t * 9.0f) * 0.15f;
        p->thigh_l = 0.4f + w; p->shin_l = 0.8f;
        p->thigh_r = 0.1f - w; p->shin_r = 0.5f;
        p->raise_l = p->raise_r = 0.35f + w;
        p->arm_l = 0.2f; p->arm_r = 0.2f;
        p->fore_l = p->fore_r = 0.5f;
        p->head = 0.15f;
    } break;
    case ANIM_GLIDE: {
        float w = sinf(t * 3.0f) * 0.05f;
        p->body_pitch = 1.15f;            /* body nearly horizontal */
        p->head = -0.9f;                  /* look forward */
        p->raise_l = 0.9f + w; p->raise_r = 0.9f - w;   /* arms spread wide */
        p->arm_l = p->arm_r = -0.15f;
        p->fore_l = p->fore_r = 0.1f;
        p->thigh_l = -0.15f; p->thigh_r = -0.25f;
        p->shin_l = 0.3f; p->shin_r = 0.5f;
        p->foot_l = p->foot_r = -0.5f;
        p->spread_l = p->spread_r = 0.12f;
    } break;
    case ANIM_WALLRUN: {
        /* running straight up a wall; the body is pitched by the game code */
        p->lean = 0.25f; p->head = -0.3f;
        p->thigh_l = 0.95f * s;  p->thigh_r = -0.95f * s;
        p->shin_l = 0.4f + 1.1f * (0.5f - 0.5f * c);
        p->shin_r = 0.4f + 1.1f * (0.5f + 0.5f * c);
        p->arm_l = -0.9f * s; p->arm_r = 0.9f * s;
        p->fore_l = p->fore_r = 1.3f;
        p->raise_l = p->raise_r = ARM_REST + 0.3f;
    } break;
    case ANIM_LEDGE: {
        float k = smooth01(t / 0.3f);
        p->lean = 0.5f * (1 - k);
        p->thigh_l = 1.2f * (1 - k); p->shin_l = 1.6f * (1 - k);
        p->thigh_r = 0.6f * (1 - k); p->shin_r = 1.0f * (1 - k);
        p->arm_l = p->arm_r = 1.4f * (1 - k) - 0.1f;
        p->fore_l = p->fore_r = 0.3f;
    } break;
    case ANIM_LAND: {
        float k = 1.0f - smooth01(t / 0.3f);
        p->lean = 0.45f * k;
        p->thigh_l = p->thigh_r = 0.9f * k;
        p->shin_l = p->shin_r = 1.6f * k;
        p->foot_l = p->foot_r = -0.6f * k;
        p->bob = -0.38f * k;
        p->raise_l = p->raise_r = ARM_REST + 0.6f * k;
        p->arm_l = p->arm_r = 0.3f * k;
    } break;
    case ANIM_SLAM: {
        /* fists overhead, then driven into the ground */
        float k = smooth01(t / 0.15f);
        p->lean = 0.6f * k; p->head = -0.4f * k;
        p->thigh_l = p->thigh_r = 1.0f * k;
        p->shin_l = p->shin_r = 1.8f * k;
        p->arm_l = p->arm_r = 2.6f * (1 - k) + 1.2f * k;
        p->raise_l = p->raise_r = ARM_REST + 0.2f;
        p->fore_l = p->fore_r = 0.2f;
        p->bob = -0.2f * k;
    } break;
    case ANIM_DODGE: {
        p->lean = 1.0f; p->head = 0.6f;
        p->thigh_l = p->thigh_r = 1.6f;
        p->shin_l = p->shin_r = 2.2f;
        p->arm_l = p->arm_r = 0.9f;
        p->fore_l = p->fore_r = 1.6f;
        p->raise_l = p->raise_r = ARM_REST + 0.2f;
    } break;
    case ANIM_PUNCH_L: case ANIM_PUNCH_R: {
        int left = a->cur == ANIM_PUNCH_L;
        float k = t < 0.08f ? t / 0.08f : 1.0f - smooth01((t - 0.12f) / 0.2f);
        k = clampf(k, 0, 1);
        float tw = (left ? -0.55f : 0.55f) * k;
        p->twist = tw; p->lean = 0.12f + 0.18f * k;
        p->bob = -0.06f * k;
        p->thigh_l = 0.35f; p->thigh_r = -0.3f; p->shin_l = 0.35f; p->shin_r = 0.25f;
        p->spread_l = p->spread_r = 0.08f;
        if (left) { p->arm_l = 1.45f * k + 0.2f; p->fore_l = 1.6f * (1 - k) + 0.05f; p->raise_l = ARM_REST + 0.45f;
                    p->arm_r = 0.6f; p->fore_r = 1.8f; }
        else      { p->arm_r = 1.45f * k + 0.2f; p->fore_r = 1.6f * (1 - k) + 0.05f; p->raise_r = ARM_REST + 0.45f;
                    p->arm_l = 0.6f; p->fore_l = 1.8f; }
    } break;
    case ANIM_KICK: {
        float k = t < 0.1f ? t / 0.1f : 1.0f - smooth01((t - 0.15f) / 0.25f);
        k = clampf(k, 0, 1);
        p->lean = -0.35f * k; p->twist = 0.4f * k;
        p->thigh_r = 1.6f * k; p->shin_r = 0.2f;
        p->thigh_l = -0.1f; p->shin_l = 0.25f;
        p->raise_l = 0.1f; p->raise_r = 0.1f;
        p->arm_l = 0.3f; p->arm_r = -0.4f;
        p->fore_l = p->fore_r = 1.2f;
    } break;
    case ANIM_COUNTER: {
        /* catch & spin: arm blocks up, then backhand */
        float k = smooth01(t / 0.12f), r = smooth01((t - 0.15f) / 0.15f);
        p->twist = -0.7f * k + 1.2f * r;
        p->arm_l = 1.6f * k; p->fore_l = 1.5f * k; p->raise_l = ARM_REST + 0.6f * k;
        p->arm_r = 1.2f * r; p->raise_r = ARM_REST + 1.0f * r; p->fore_r = 0.3f;
        p->thigh_l = 0.3f; p->shin_l = 0.4f; p->thigh_r = -0.2f; p->shin_r = 0.4f;
        p->lean = 0.1f;
    } break;
    case ANIM_BLAST: {
        float k = smooth01(t / 0.1f);
        p->arm_r = 1.55f * k; p->fore_r = 0.05f; p->raise_r = ARM_REST + 0.5f * k;
        p->arm_l = 0.5f * k; p->fore_l = 1.4f;
        p->twist = 0.35f * k; p->lean = -0.05f;
        p->thigh_l = 0.3f; p->thigh_r = -0.25f; p->shin_l = p->shin_r = 0.3f;
    } break;
    case ANIM_HIT: {
        float k = 1.0f - smooth01(t / 0.35f);
        p->lean = -0.4f * k; p->head = 0.4f * k;
        p->raise_l = p->raise_r = ARM_REST + 0.5f * k;
        p->arm_l = p->arm_r = -0.4f * k;
        p->shin_l = p->shin_r = 0.4f * k;
    } break;
    case ANIM_AIM: {
        /* right arm out towards the reticle, left hand guarding; legs walk if moving */
        float amp = clampf(a->speed / 4.0f, 0, 1);
        p->arm_r = 1.45f; p->fore_r = 0.05f; p->raise_r = ARM_REST + 0.3f;
        p->arm_l = 0.6f; p->fore_l = 1.7f; p->raise_l = ARM_REST + 0.2f;
        p->twist = 0.25f; p->lean = 0.05f;
        p->thigh_l = 0.45f * s * amp + 0.1f; p->thigh_r = -0.45f * s * amp - 0.1f;
        p->shin_l = 0.2f + 0.5f * (0.5f - 0.5f * c) * amp; p->shin_r = 0.2f + 0.5f * (0.5f + 0.5f * c) * amp;
        p->bob = -0.03f - 0.02f * fabsf(c) * amp;
    } break;
    case ANIM_SKID: {
        /* braking hard: lean back, feet planted ahead, arms out for balance */
        float k = smooth01(t / 0.08f) * (1.0f - smooth01((t - 0.2f) / 0.15f));
        p->lean = -0.35f * k; p->head = 0.15f * k;
        p->thigh_l = 0.75f * k; p->shin_l = 0.35f * k; p->foot_l = 0.3f * k;
        p->thigh_r = 0.2f * k; p->shin_r = 0.9f * k;
        p->bob = -0.18f * k;
        p->raise_l = p->raise_r = ARM_REST + 0.7f * k;
        p->arm_l = p->arm_r = 0.3f * k; p->fore_l = p->fore_r = 0.6f;
    } break;
    case ANIM_LAND_SOFT: {
        float k = sinf(clampf(t / 0.2f, 0, 1) * 3.1416f);
        p->lean = 0.2f * k;
        p->thigh_l = p->thigh_r = 0.4f * k; p->shin_l = p->shin_r = 0.8f * k;
        p->foot_l = p->foot_r = -0.3f * k;
        p->bob = -0.16f * k;
        p->raise_l = p->raise_r = ARM_REST + 0.3f * k;
    } break;
    case ANIM_THROW: {
        /* overhand toss of a smoke bomb */
        float k = t < 0.12f ? smooth01(t / 0.12f) : 1.0f - smooth01((t - 0.12f) / 0.25f);
        float wind = 1.0f - smooth01(t / 0.12f);
        p->arm_r = -1.8f * wind + 2.0f * (1 - wind) * k; p->fore_r = 0.9f * wind + 0.2f;
        p->raise_r = ARM_REST + 0.4f;
        p->twist = 0.4f * wind - 0.3f * k; p->lean = 0.25f * k;
        p->thigh_l = 0.45f; p->shin_l = 0.3f; p->thigh_r = -0.3f; p->shin_r = 0.35f;
        p->arm_l = 0.5f; p->fore_l = 1.2f;
    } break;
    default: break;
    }
}

void anim_update(Animator *a, float dt) {
    a->t += dt;
    /* locomotion phase advances with speed (stride ~ 1.9m per cycle at run) */
    float stride = a->cur == ANIM_SPRINT ? 3.6f : (a->cur == ANIM_WALLRUN ? 2.6f : 1.9f);
    a->phase += dt * (a->speed / stride) * 2.0f * (float)M_PI;
    if (a->phase > 2 * M_PI) a->phase -= 2 * (float)M_PI;
    if (a->blend < 1) a->blend = a->blend_time > 0 ? clampf(a->blend + dt / a->blend_time, 0, 1) : 1;
    AnimParams target;
    pose_of(a, &target);
    float k = smooth01(a->blend);
    for (int i = 0; i < AP_COUNT; i++) a->out.v[i] = lerpf(a->prev.v[i], target.v[i], k);
}

void anim_apply(const Rig *r, const AnimParams *p, Pose *pose) {
    pose_reset(pose);
    if (!r->bones_ok) return;
    /* spine: hips get a little, chest more */
    pose_rotate(pose, r->spine[0], m4mul(m4roty(p->twist * -0.3f), m4rotz(p->hip_side * 0.5f)));
    pose_rotate(pose, r->spine[1], m4rotx(p->lean * 0.35f));
    pose_rotate(pose, r->spine[2], m4mul(m4rotx(p->lean * 0.35f + p->chest), m4roty(p->twist * 0.5f)));
    pose_rotate(pose, r->spine[3], m4mul(m4rotx(p->lean * 0.3f + p->chest), m4roty(p->twist * 0.5f)));
    pose_rotate(pose, r->spine[4], m4rotx(p->head * 0.5f));
    pose_rotate(pose, r->spine[5], m4rotx(p->head * 0.5f - p->lean * 0.3f));

    const float sgn[2] = {1, -1};
    const float thigh[2] = {p->thigh_l, p->thigh_r}, spread[2] = {p->spread_l, p->spread_r};
    const float shin[2] = {p->shin_l, p->shin_r}, foot[2] = {p->foot_l, p->foot_r};
    const float arm[2] = {p->arm_l, p->arm_r}, raise[2] = {p->raise_l, p->raise_r}, fore[2] = {p->fore_l, p->fore_r};
    for (int s = 0; s < 2; s++) {
        /* +thigh swings the leg forward, +shin bends the knee */
        pose_rotate(pose, r->thigh[s], m4mul(m4rotx(-thigh[s]), m4rotz(spread[s] * sgn[s])));
        pose_rotate(pose, r->shin[s], m4rotx(shin[s]));
        pose_rotate(pose, r->foot[s], m4rotx(foot[s] - shin[s] * 0.3f));
        /* +raise lifts the arm sideways, +arm swings forward, +fore bends the elbow */
        pose_rotate(pose, r->upper[s], m4mul(m4rotx(-arm[s]), m4rotz(raise[s] * sgn[s])));
        pose_rotate(pose, r->fore[s], m4rotx(-fore[s]));
    }
}
