/* Code-driven animation. Each animation produces a small set of joint angles
 * (AnimParams); these are blended for smooth transitions and then turned into
 * bone rotations. Replace with keyframed clips later if desired. */
#ifndef ANIM_H
#define ANIM_H
#include "model.h"

typedef enum {
    ANIM_IDLE, ANIM_RUN, ANIM_SPRINT, ANIM_JUMP, ANIM_FALL, ANIM_GLIDE,
    ANIM_WALLRUN, ANIM_LAND, ANIM_SLAM, ANIM_DODGE, ANIM_LEDGE,
    ANIM_PUNCH_L, ANIM_PUNCH_R, ANIM_KICK, ANIM_COUNTER, ANIM_BLAST, ANIM_HIT,
    ANIM_COUNT
} AnimId;

#define AP_COUNT 22
typedef union {
    struct {
        float lean, twist, chest, head, bob, body_pitch, body_roll;
        float thigh_l, thigh_r, spread_l, spread_r, shin_l, shin_r, foot_l, foot_r;
        float arm_l, arm_r, raise_l, raise_r, fore_l, fore_r;
        float hip_side;
    };
    float v[AP_COUNT];
} AnimParams;

typedef struct {
    int bones_ok;
    int spine[7], thigh[2], shin[2], foot[2], upper[2], fore[2], shoulder[2], hand[2];
} Rig;

typedef struct {
    AnimId cur;
    float t;            /* time in current animation */
    float phase;        /* locomotion cycle phase */
    float blend;        /* 0..1 blend from prev to cur */
    float blend_time;
    AnimParams prev, out;
    float speed;        /* m/s, drives locomotion */
} Animator;

void anim_rig(Rig *r, const Model *m);
void anim_init(Animator *a);
void anim_play(Animator *a, AnimId id, float blend_time);
void anim_update(Animator *a, float dt);
void anim_apply(const Rig *r, const AnimParams *p, Pose *pose);

#endif
