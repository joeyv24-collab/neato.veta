#include <psp2/kernel/processmgr.h>
#include "platform.h"
#include "gfx.h"
#include "game.h"

void game_fps_tick(float dt);

#if NEATO_PAD > 0
__attribute__((used)) static const char segment_pad[NEATO_PAD] = {1};
#endif

int main(void) {
    plat_init();
    gfx_init();
    if (!game_init()) {
        /* assets missing: show a red screen for a few seconds then quit */
        for (int i = 0; i < 180; i++) { gfx_begin_frame(0.6f, 0.1f, 0.1f); gfx_end_frame(); }
        gfx_shutdown();
        sceKernelExitProcess(0);
        return 0;
    }
    Input in = {0};
    double last = plat_time();
    while (plat_running()) {
        double now = plat_time();
        float dt = (float)(now - last);
        last = now;
        if (dt > 0.05f) dt = 0.05f;          /* avoid huge steps after a stall */
        plat_poll(&in);
        game_update(&in, dt);
        game_fps_tick(dt);
        game_render();
    }
    gfx_shutdown();
    sceKernelExitProcess(0);
    return 0;
}
