#ifndef PLATFORM_H
#define PLATFORM_H
#include <stdint.h>

enum {
    BTN_CROSS = 1 << 0, BTN_CIRCLE = 1 << 1, BTN_SQUARE = 1 << 2, BTN_TRIANGLE = 1 << 3,
    BTN_L = 1 << 4, BTN_R = 1 << 5, BTN_START = 1 << 6, BTN_SELECT = 1 << 7,
    BTN_UP = 1 << 8, BTN_DOWN = 1 << 9, BTN_LEFT = 1 << 10, BTN_RIGHT = 1 << 11,
};

typedef struct {
    float lx, ly, rx, ry;       /* sticks, -1..1, y down = +1 */
    uint32_t held, pressed, released;
    int touching;               /* front touch */
    float tx, ty;               /* touch position in screen pixels */
} Input;

int    plat_init(void);
void   plat_shutdown(void);
int    plat_running(void);
void   plat_poll(Input *in);
double plat_time(void);         /* seconds */
/* loads a whole file from the asset folder; caller frees */
void  *plat_load(const char *name, int *size);

#endif
