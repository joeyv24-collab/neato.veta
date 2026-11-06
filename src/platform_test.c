/* Desktop test platform: no window. Input comes from a script and frames are
 * written to disk by the software renderer. */
#include <stdio.h>
#include <stdlib.h>
#include "platform.h"

static double now = 0;
static Input scripted;
static int running = 1;

int plat_init(void) { return 1; }
void plat_shutdown(void) {}
int plat_running(void) { return running; }
void plat_quit(void) { running = 0; }
double plat_time(void) { return now; }
void plat_advance(double dt) { now += dt; }

void plat_set_input(const Input *in) { scripted = *in; }
void plat_poll(Input *in) {
    uint32_t prev = in->held;
    *in = scripted;
    in->pressed = in->held & ~prev;
    in->released = prev & ~in->held;
}

void *plat_load(const char *name, int *size) {
    char path[256];
    snprintf(path, sizeof path, "assets/%s", name);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "missing asset %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    void *d = malloc(n);
    if (fread(d, 1, n, f) != (size_t)n) { fclose(f); free(d); return NULL; }
    fclose(f);
    if (size) *size = (int)n;
    return d;
}
