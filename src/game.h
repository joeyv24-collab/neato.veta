#ifndef GAME_H
#define GAME_H
#include "platform.h"

int  game_init(void);
void game_update(const Input *in, float dt);
void game_render(void);

/* exposed for the desktop test harness */
typedef struct { float x, y, z, yaw; int state; float charge; } GameDebug;
void game_debug(GameDebug *d);

#endif
