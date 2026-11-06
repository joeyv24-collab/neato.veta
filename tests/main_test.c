/* Desktop test: plays a scripted input sequence through the real game code,
 * logs the player state and saves screenshots (software renderer). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/game.h"
#include "../src/platform.h"

void plat_set_input(const Input *in);
void plat_advance(double dt);
void gfx_soft_save(const char *path);
void game_fps_tick(float dt);

typedef struct { float t0, t1; float lx, ly, rx, ry; unsigned held; } Seg;

/* timeline (seconds) */
static const Seg script[] = {
    {0.0f, 1.0f, 0, 0, 0, 0, 0},                       /* idle */
    {1.0f, 1.8f, 0, -1, 0, 0, 0},                      /* run forward */
    {1.8f, 6.0f, 0, -1, 0, 0, BTN_R},                  /* sprint into a building -> wall run */
    {6.0f, 6.6f, 0, -1, 0, 0, 0},                      /* run across the roof */
    {6.6f, 6.7f, 0, -1, 0, 0, BTN_CROSS},              /* jump */
    {6.7f, 6.9f, 0, -1, 0, 0, 0},
    {6.9f, 9.0f, 0, -1, 0, 0, BTN_CROSS},              /* glide */
    {9.3f, 9.4f, 0, 0, 0, 0, BTN_CROSS},               /* hop */
    {9.6f, 9.7f, 0, 0, 0, 0, BTN_CIRCLE},              /* ground slam */
    {10.2f, 10.3f, 0, 0, 0, 0, BTN_CIRCLE},            /* dodge roll */
    {10.5f, 11.0f, 0, 0, 0.6f, 0, 0},                   /* turn camera */
    {11.0f, 11.1f, 0, 0, 0, 0, BTN_SQUARE},            /* combo */
    {11.25f, 11.3f, 0, 0, 0, 0, BTN_SQUARE},
    {11.6f, 11.65f, 0, 0, 0, 0, BTN_SQUARE},
    {12.2f, 12.3f, 0, 0, 0, 0, BTN_TRIANGLE},          /* kinetic blast */
    {12.6f, 13.0f, 0, 0, 0, 0, 0},
};
static const float shots[] = {0.5f, 1.6f, 2.6f, 3.4f, 4.6f, 6.3f, 7.6f, 8.6f, 9.8f, 10.35f, 11.7f, 12.4f};

int main(void) {
    plat_init();
    if (!game_init()) { printf("init failed\n"); return 1; }
    const float dt = 1.0f / 60.0f;
    int shot = 0, nseg = sizeof script / sizeof script[0], nshot = sizeof shots / sizeof shots[0];
    float t = 0;
    static const char *names[] = {"GROUND", "AIR", "GLIDE", "WALLRUN", "LEDGE", "SLAM", "DODGE", "ATTACK", "LAND"};
    int last_state = -1;
    while (t < 13.0f) {
        Input in; memset(&in, 0, sizeof in);
        for (int i = 0; i < nseg; i++)
            if (t >= script[i].t0 && t < script[i].t1) {
                in.lx = script[i].lx; in.ly = script[i].ly; in.rx = script[i].rx; in.ry = script[i].ry;
                in.held = script[i].held;
            }
        plat_set_input(&in);
        static Input cur;
        plat_poll(&cur);
        game_update(&cur, dt);
        game_fps_tick(dt);
        GameDebug d; game_debug(&d);
        if (d.state != last_state) {
            printf("t=%5.2f %-8s pos=(%6.1f %6.1f %6.1f) charge=%.0f\n", t, names[d.state], d.x, d.y, d.z, d.charge);
            last_state = d.state;
        }
        if (shot < nshot && t >= shots[shot]) {
            game_render();
            char p[64]; snprintf(p, sizeof p, "/tmp/shot_%02d.ppm", shot);
            gfx_soft_save(p);
            shot++;
        }
        t += dt;
        plat_advance(dt);
    }
    extern int game_show_hud, game_show_player;
    game_show_hud = 0; game_show_player = 0;
    game_render(); gfx_soft_save("/tmp/clean.ppm");
    GameDebug d; game_debug(&d);
    printf("end pos=(%.1f %.1f %.1f)\n", d.x, d.y, d.z);
    return 0;
}
