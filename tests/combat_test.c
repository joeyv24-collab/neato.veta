/* Scripted fight against the first checkpoint, logs and screenshots. */
#include <stdio.h>
#include <string.h>
#include "../src/game.h"
#include "../src/platform.h"
void plat_set_input(const Input *in);
void plat_advance(double dt);
void gfx_soft_save(const char *path);
void game_fps_tick(float dt);
extern int game_skip_intro;

int main(void) {
    game_skip_intro = 1;
    plat_init();
    if (!game_init()) return 1;
    const float dt = 1 / 60.0f;
    const float shots[] = {1.2f, 2.3f, 3.1f, 4.0f, 5.2f, 6.5f, 8.0f, 10.0f, 12.5f};
    int shot = 0, nshot = sizeof shots / sizeof shots[0];
    GameDebug last = {0};
    for (int f = 0; f < 60 * 16; f++) {
        float t = f * dt;
        Input in; memset(&in, 0, sizeof in);
        if (t > 0.8f && t < 1.8f) in.ly = -1;                 /* walk in */
        if (t > 2.0f) {
            int k = (int)((t - 2.0f) / 0.2f);                  /* mash strike, counter every 3rd beat */
            float ph = (t - 2.0f) - k * 0.2f;
            if (ph < 0.05f) in.held = (k % 3 == 2) ? BTN_TRIANGLE : BTN_SQUARE;
        }
        if (t > 9.0f && t < 9.3f) in.held = BTN_L;           /* scanner */
        plat_set_input(&in);
        static Input cur; plat_poll(&cur);
        game_update(&cur, dt);
        game_fps_tick(dt);
        GameDebug d; game_debug(&d);
        if (d.guards_down != last.guards_down || (int)(d.health) / 10 != (int)(last.health) / 10 || d.cleared != last.cleared)
            printf("t=%5.2f hp=%3.0f charge=%3.0f down=%d cleared=%d best_chain=%d state=%d\n", t, d.health, d.charge, d.guards_down, d.cleared, d.chain, d.state);
        last = d;
        if (shot < nshot && t >= shots[shot]) {
            game_render();
            char p[64]; snprintf(p, sizeof p, "/tmp/cshot_%02d.ppm", shot++);
            gfx_soft_save(p);
        }
        plat_advance(dt);
    }
    return 0;
}
