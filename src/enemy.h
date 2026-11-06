/* Vantage Security guards: the private security force holding Saltreach.
 * Low-poly box-built bodies animated in code, grouped at checkpoints. */
#ifndef ENEMY_H
#define ENEMY_H
#include "vmath.h"
#include "world.h"

typedef enum {
    E_PATROL, E_CHASE, E_CIRCLE, E_WINDUP, E_ATTACK, E_RECOVER, E_STAGGER, E_KNOCK, E_DOWN
} EState;

typedef struct {
    vec3 pos, vel, home;
    float yaw;
    EState st; float t;
    int hp, group, heavy;
    float phase, flash, cooldown, circle_ang, look_t;
    vec3 knock;
    float knock_y, roll;
} Enemy;

#define MAX_ENEMIES 48
#define MAX_GROUPS  8

typedef struct {
    Enemy e[MAX_ENEMIES]; int n;
    int ngroups;
    vec3 group_pos[MAX_GROUPS];
    int group_alert[MAX_GROUPS];
    int group_left[MAX_GROUPS];      /* guards still standing */
    int tex_white, tex_dot;
} Enemies;

/* what the player is doing, as far as the guards care */
typedef struct {
    vec3 pos;
    int invulnerable;
    int hidden;          /* on a rooftop / perched: much harder to spot */
} PlayerInfo;

void  enemies_init(Enemies *es, World *w, unsigned seed);
void  enemies_reset(Enemies *es);           /* after the player is defeated */
/* returns damage dealt to the player this frame; *hit_from is the attacker */
float enemies_update(Enemies *es, const World *w, const PlayerInfo *pi, float dt, int *hit_from);
void  enemies_draw(Enemies *es, const mat4 *proj, const mat4 *view, vec3 cam, float range, int xray);

/* combat queries */
int   enemies_target(const Enemies *es, vec3 from, vec3 dir, float range);   /* best strike target */
int   enemies_counterable(const Enemies *es, vec3 from, float range);        /* guard mid wind-up */
int   enemy_alive(const Enemy *e);
int   enemy_aware(const Enemies *es, int i);
/* damage: knock is horizontal push speed, lift is vertical. returns 1 if the guard went down */
int   enemy_hit(Enemies *es, int i, int dmg, vec3 from, float knock, float lift);
void  enemies_alert_group(Enemies *es, int group);
void  enemy_takedown(Enemies *es, int i);
int   enemies_groups_cleared(const Enemies *es);
int   enemies_attackers(const Enemies *es);

#endif
