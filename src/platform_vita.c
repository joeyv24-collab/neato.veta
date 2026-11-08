#include <stdio.h>
#include <stdlib.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/power.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/stat.h>
#include "platform.h"

/* larger newlib heap for assets and dynamic buffers */
int _newlib_heap_size_user = 128 * 1024 * 1024;

int plat_init(void) {
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    return 1;
}

void plat_shutdown(void) {}

static int running = 1;
int plat_running(void) { return running; }

static float stick(unsigned char v) {
    float f = ((int)v - 128) / 127.0f;
    return f < -1 ? -1 : (f > 1 ? 1 : f);
}

void plat_poll(Input *in) {
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(0, &pad, 1);
    uint32_t prev = in->held, h = 0;
    if (pad.buttons & SCE_CTRL_CROSS) h |= BTN_CROSS;
    if (pad.buttons & SCE_CTRL_CIRCLE) h |= BTN_CIRCLE;
    if (pad.buttons & SCE_CTRL_SQUARE) h |= BTN_SQUARE;
    if (pad.buttons & SCE_CTRL_TRIANGLE) h |= BTN_TRIANGLE;
    if (pad.buttons & SCE_CTRL_LTRIGGER) h |= BTN_L;
    if (pad.buttons & SCE_CTRL_RTRIGGER) h |= BTN_R;
    if (pad.buttons & SCE_CTRL_START) h |= BTN_START;
    if (pad.buttons & SCE_CTRL_SELECT) h |= BTN_SELECT;
    if (pad.buttons & SCE_CTRL_UP) h |= BTN_UP;
    if (pad.buttons & SCE_CTRL_DOWN) h |= BTN_DOWN;
    if (pad.buttons & SCE_CTRL_LEFT) h |= BTN_LEFT;
    if (pad.buttons & SCE_CTRL_RIGHT) h |= BTN_RIGHT;
    in->held = h;
    in->pressed = h & ~prev;
    in->released = prev & ~h;
    in->lx = stick(pad.lx); in->ly = stick(pad.ly);
    in->rx = stick(pad.rx); in->ry = stick(pad.ry);

    SceTouchData touch;
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
    in->touching = touch.reportNum > 0;
    if (in->touching) {          /* touch panel is 1920x1088 */
        in->tx = touch.report[0].x * 0.5f;
        in->ty = touch.report[0].y * 0.5f;
    }
    if ((h & BTN_START) && (h & BTN_SELECT)) running = 0;
}

double plat_time(void) {
    return sceKernelGetProcessTimeWide() / 1000000.0;
}

void *plat_load(const char *name, int *size) {
    char path[256];
    snprintf(path, sizeof path, "app0:assets/%s", name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *d = malloc(n);
    if (fread(d, 1, n, f) != (size_t)n) { fclose(f); free(d); return NULL; }
    fclose(f);
    if (size) *size = (int)n;
    return d;
}

#define SAVE_DIR  "ux0:data/neato"
#define SAVE_FILE SAVE_DIR "/save.bin"
int plat_save_write(const void *data, int size) {
    sceIoMkdir(SAVE_DIR, 0777);
    FILE *f = fopen(SAVE_FILE, "wb"); if (!f) return 0;
    int ok = fwrite(data, 1, size, f) == (size_t)size; fclose(f); return ok;
}
int plat_save_read(void *data, int size) {
    FILE *f = fopen(SAVE_FILE, "rb"); if (!f) return 0;
    int ok = fread(data, 1, size, f) == (size_t)size; fclose(f); return ok;
}
