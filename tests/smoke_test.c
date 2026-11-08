/* Smoke powers: aim + bolts at the first checkpoint, smoke bomb, takedowns in smoke, mist dash, skid. */
#include <stdio.h>
#include <string.h>
#include "../src/game.h"
#include "../src/platform.h"
void plat_set_input(const Input *in); void plat_advance(double dt); void gfx_soft_save(const char *p); void game_fps_tick(float);
extern int game_skip_intro;
static Input cur; static float T; static int shotn;
static void step(unsigned held, float lx, float ly, float secs) {
    for (int i = 0; i < (int)(secs * 60); i++) {
        Input in; memset(&in, 0, sizeof in); in.held = held; in.lx = lx; in.ly = ly;
        plat_set_input(&in); plat_poll(&cur); game_update(&cur, 1 / 60.0f); game_fps_tick(1 / 60.0f); plat_advance(1 / 60.0f); T += 1 / 60.0f;
    }
}
static void shot(void) { game_render(); char p[64]; snprintf(p, 64, "/tmp/sshot_%02d.ppm", shotn++); gfx_soft_save(p); }
static void log_(const char *s) { GameDebug d; game_debug(&d); printf("%-14s t=%5.1f pos=(%5.1f %4.1f %5.1f) st=%d hp=%3.0f mist=%3.0f down=%d\n", s, T, d.x, d.y, d.z, d.state, d.health, d.charge, d.guards_down); }
int main(void) {
    game_skip_intro = 1; plat_init(); game_init();
    step(0, 0, 0, 3.5f);                         /* let mist build */
    step(BTN_R, 0.45f, 1, 1.4f); log_("sprinted");  /* sprint south */
    step(0, 0, 0, 0.4f); log_("skid"); shot();   /* skid stop */
    { Input in; memset(&in, 0, sizeof in); in.rx = 1; for (int i = 0; i < 67; i++) { plat_set_input(&in); plat_poll(&cur); game_update(&cur, 1 / 60.0f); plat_advance(1 / 60.0f); T += 1 / 60.0f; } }
    step(BTN_L, 0, 0, 0.5f); shot();             /* turn the camera round, then aim */
    for (int k = 0; k < 8; k++) { step(BTN_L | BTN_R, 0, 0, 0.1f); step(BTN_L, 0, 0, 0.15f); }
    log_("after bolts"); shot();
    step(0, 0, 0, 3.0f); step(0, 0, -1, 1.2f); log_("closing in");
    step(BTN_TRIANGLE, 0, 0, 0.05f); step(0, 0, 0, 0.6f); log_("smoke bomb"); shot();
    for (int k = 0; k < 6; k++) { step(BTN_SQUARE, 0, 0, 0.05f); step(0, 0, 0, 0.4f); }
    log_("in smoke"); shot();
    step(BTN_CIRCLE, 0, 1, 0.05f); step(0, 0, 1, 0.12f); shot(); step(0, 0, 0, 0.4f); log_("dash");
    step(0, 0, 0, 1.0f); shot();
    return 0;
}
