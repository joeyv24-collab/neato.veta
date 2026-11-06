#ifndef WORLD_H
#define WORLD_H
#include "vmath.h"
#include "gfx.h"

/* The playable district: a grid of city blocks on an island surrounded by
 * flood water. Everything is generated from a seed so no level files are
 * needed yet. */

#define LAND_HALF   180.0f      /* land spans -LAND_HALF..LAND_HALF on X and Z */
#define BLOCK_PITCH 48.0f       /* block + road */
#define ROAD_W      12.0f
#define WATER_Y     (-1.2f)

enum { BOX_BUILDING, BOX_PROP, BOX_SEAWALL };

typedef struct { vec3 mn, mx; int kind; } Box;

#define MAX_BOXES   1536
#define GRID_N      16
#define GRID_CELL   (2 * LAND_HALF / GRID_N)
#define CELL_MAX    48
#define MAX_CHUNKS  128

typedef struct {
    int mesh_tex, mesh_flat;    /* static meshes: window-textured walls, flat colour */
    vec3 center; float radius;
} Chunk;

typedef struct {
    Box box[MAX_BOXES]; int nbox;
    short cell[GRID_N][GRID_N][CELL_MAX]; unsigned char ncell[GRID_N][GRID_N];
    Chunk chunk[MAX_CHUNKS]; int nchunk;
    int ground_mesh, water_mesh;
    int tex_facade, tex_ground, tex_white, tex_water;
    vec3 spawn;
    float time;
} World;

void world_build(World *w, unsigned seed);
void world_draw(World *w, const mat4 *proj, const mat4 *view, vec3 cam, vec3 cam_fwd, float fog_end);

/* collision helpers */
int   world_query(const World *w, vec3 mn, vec3 mx, int *out, int max);
/* highest walkable surface under (x,z) that is at or below y (returns -1000 for open water) */
float world_floor(const World *w, float x, float z, float y, float radius);
/* segment cast against boxes; returns 1 and fraction t on hit */
int   world_raycast(const World *w, vec3 a, vec3 b, float *t, vec3 *normal);
int   world_on_land(float x, float z);

#endif
