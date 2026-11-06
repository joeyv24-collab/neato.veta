#include <stdlib.h>
#include <string.h>
#include "world.h"

/* ------------------------------------------------------------------ */
/* procedural textures                                                 */

static int make_facade(void) {
    enum { S = 64 };
    static uint8_t px[S * S * 4];
    unsigned r = 1234;
    /* one tile = 2x2 windows; walls are white so vertex colours tint them */
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
        int wx = x % 32, wy = y % 32;
        int win = wx >= 7 && wx < 25 && wy >= 6 && wy < 24;
        uint8_t *p = &px[(y * S + x) * 4];
        float n = 0.92f + 0.08f * rng_float(&r);
        if (win) {
            int lit = ((x / 32) + (y / 32) * 3) % 3 == 0;
            float g = wy < 14 ? 0.85f : 1.0f;       /* glass reflection band */
            if (lit) { p[0] = (uint8_t)(255 * g); p[1] = (uint8_t)(205 * g); p[2] = (uint8_t)(120 * g); }
            else { p[0] = (uint8_t)(40 * g + 20); p[1] = (uint8_t)(55 * g + 20); p[2] = (uint8_t)(75 * g + 25); }
            if (wx == 15 || wx == 16 || wy == 14) { p[0] = 70; p[1] = 70; p[2] = 75; } /* mullions */
        } else {
            int sill = wy == 24 || wy == 25;
            float v = (sill ? 0.7f : 1.0f) * n;
            p[0] = p[1] = p[2] = (uint8_t)(235 * v);
        }
        p[3] = 255;
    }
    return gfx_texture(S, S, px, 1, 1);
}

static int make_ground(void) {
    /* 128px covers one BLOCK_PITCH (48m): sidewalk + block, road, lane dashes */
    enum { S = 128 };
    static uint8_t px[S * S * 4];
    unsigned r = 99;
    float road_px = ROAD_W / BLOCK_PITCH * S;           /* road at the start of each tile */
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
        uint8_t *p = &px[(y * S + x) * 4];
        int road_x = x < road_px, road_y = y < road_px;
        float n = 0.9f + 0.1f * rng_float(&r);
        int c;
        if (road_x || road_y) {
            c = (int)(70 * n);
            int mid = (int)(road_px / 2);
            /* dashed centre lines, not inside intersections */
            if (road_x && !road_y && (x == mid || x == mid - 1) && (y / 6) % 2) { p[0] = 220; p[1] = 190; p[2] = 80; p[3] = 255; continue; }
            if (road_y && !road_x && (y == mid || y == mid - 1) && (x / 6) % 2) { p[0] = 220; p[1] = 190; p[2] = 80; p[3] = 255; continue; }
            /* crosswalk stripes near intersections */
            if (road_x && !road_y && y < road_px + 6 && (x / 2) % 2) c = 200;
            if (road_y && !road_x && x < road_px + 6 && (y / 2) % 2) c = 200;
        } else {
            int curb = x < road_px + 2 || y < road_px + 2;
            int slab = ((x % 8) == 0 || (y % 8) == 0);
            c = (int)((curb ? 175 : (slab ? 140 : 155)) * n);
        }
        p[0] = (uint8_t)c; p[1] = (uint8_t)c; p[2] = (uint8_t)(c + 4 > 255 ? 255 : c + 4); p[3] = 255;
    }
    return gfx_texture(S, S, px, 1, 1);
}

static int make_water(void) {
    enum { S = 64 };
    static uint8_t px[S * S * 4];
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
        float w = sinf(x * 0.39f + sinf(y * 0.2f) * 2) * 0.5f + sinf(y * 0.29f + x * 0.1f) * 0.5f;
        uint8_t *p = &px[(y * S + x) * 4];
        p[0] = (uint8_t)(40 + 20 * w); p[1] = (uint8_t)(90 + 25 * w); p[2] = (uint8_t)(105 + 25 * w); p[3] = 200;
    }
    return gfx_texture(S, S, px, 1, 1);
}

/* ------------------------------------------------------------------ */
/* geometry builder                                                    */

typedef struct { GVert *v; uint16_t *i; int nv, ni, cv, ci; } Build;

static void b_reset(Build *b) { b->nv = b->ni = 0; }
static void b_quad(Build *b, vec3 p0, vec3 p1, vec3 p2, vec3 p3, float u0, float v0, float u1, float v1, uint32_t c) {
    if (b->nv + 4 > b->cv) { b->cv = b->cv ? b->cv * 2 : 1024; b->v = realloc(b->v, b->cv * sizeof(GVert)); }
    if (b->ni + 6 > b->ci) { b->ci = b->ci ? b->ci * 2 : 2048; b->i = realloc(b->i, b->ci * 2); }
    GVert q[4] = {{p0.x, p0.y, p0.z, u0, v1, c}, {p1.x, p1.y, p1.z, u1, v1, c},
                  {p2.x, p2.y, p2.z, u1, v0, c}, {p3.x, p3.y, p3.z, u0, v0, c}};
    memcpy(&b->v[b->nv], q, sizeof q);
    uint16_t s = (uint16_t)b->nv;
    uint16_t id[6] = {s, (uint16_t)(s + 1), (uint16_t)(s + 2), s, (uint16_t)(s + 2), (uint16_t)(s + 3)};
    memcpy(&b->i[b->ni], id, sizeof id);
    b->nv += 4; b->ni += 6;
}

static uint32_t shade(uint32_t c, float k) {
    int r = (int)((c & 255) * k), g = (int)(((c >> 8) & 255) * k), bl = (int)(((c >> 16) & 255) * k);
    return rgba32(r > 255 ? 255 : r, g > 255 ? 255 : g, bl > 255 ? 255 : bl, 255);
}

/* box with per-face sun shading. textured walls use metres/8 as UV */
static void b_box(Build *walls, Build *flat, vec3 a, vec3 b, uint32_t col, int windows, float uoff) {
    Build *wb = windows ? walls : flat;
    float sx = (b.x - a.x) / 8.0f, sz = (b.z - a.z) / 8.0f, sy = (b.y - a.y) / 8.0f;
    float y0 = windows ? (a.y + 0.0f) / 8.0f : 0, y1 = windows ? b.y / 8.0f : 1;
    if (!windows) { sx = sz = sy = 1; }
    (void)sy;
    /* sun from +x,+y,+z */
    uint32_t cN = shade(col, 0.95f), cS = shade(col, 0.62f), cE = shade(col, 0.82f), cW = shade(col, 0.70f);
    /* each wall: bottom-left, bottom-right, top-right, top-left as seen from outside (CCW) */
    b_quad(wb, v3(a.x, a.y, b.z), v3(b.x, a.y, b.z), v3(b.x, b.y, b.z), v3(a.x, b.y, b.z), uoff, y1, uoff + sx, y0, cN);
    b_quad(wb, v3(b.x, a.y, a.z), v3(a.x, a.y, a.z), v3(a.x, b.y, a.z), v3(b.x, b.y, a.z), uoff, y1, uoff + sx, y0, cS);
    b_quad(wb, v3(b.x, a.y, b.z), v3(b.x, a.y, a.z), v3(b.x, b.y, a.z), v3(b.x, b.y, b.z), uoff, y1, uoff + sz, y0, cE);
    b_quad(wb, v3(a.x, a.y, a.z), v3(a.x, a.y, b.z), v3(a.x, b.y, b.z), v3(a.x, b.y, a.z), uoff, y1, uoff + sz, y0, cW);
    /* roof (always flat colour, slightly darker concrete) */
    b_quad(flat, v3(a.x, b.y, b.z), v3(b.x, b.y, b.z), v3(b.x, b.y, a.z), v3(a.x, b.y, a.z), 0, 0, 1, 1,
           windows ? shade(col, 0.78f) : shade(col, 1.0f));
}

static void add_box(World *w, vec3 a, vec3 b, int kind) {
    if (w->nbox >= MAX_BOXES) return;
    Box *bx = &w->box[w->nbox];
    bx->mn = a; bx->mx = b; bx->kind = kind;
    int gx0 = (int)((a.x + LAND_HALF) / GRID_CELL), gx1 = (int)((b.x + LAND_HALF) / GRID_CELL);
    int gz0 = (int)((a.z + LAND_HALF) / GRID_CELL), gz1 = (int)((b.z + LAND_HALF) / GRID_CELL);
    for (int gx = gx0; gx <= gx1; gx++) for (int gz = gz0; gz <= gz1; gz++) {
        if (gx < 0 || gz < 0 || gx >= GRID_N || gz >= GRID_N) continue;
        if (w->ncell[gx][gz] < CELL_MAX) w->cell[gx][gz][w->ncell[gx][gz]++] = (short)w->nbox;
    }
    w->nbox++;
}

static const uint32_t PALETTE[] = {
    0xffd8d2c8, 0xffb8c4c8, 0xffa8b4c8, 0xffc8a890, 0xff98a8b0, 0xffd0c0b0, 0xff8898a8, 0xffb0b8a0,
};

static void flush_chunk(World *w, Build *walls, Build *flat, vec3 c, float rad) {
    if (w->nchunk >= MAX_CHUNKS) return;
    Chunk *ch = &w->chunk[w->nchunk++];
    ch->mesh_tex = walls->ni ? gfx_mesh(walls->v, walls->nv, walls->i, walls->ni) : 0;
    ch->mesh_flat = flat->ni ? gfx_mesh(flat->v, flat->nv, flat->i, flat->ni) : 0;
    ch->center = c; ch->radius = rad;
    b_reset(walls); b_reset(flat);
}

void world_build(World *w, unsigned seed) {
    memset(w, 0, sizeof *w);
    w->tex_facade = make_facade();
    w->tex_ground = make_ground();
    w->tex_water = make_water();
    static const uint8_t white[4] = {255, 255, 255, 255};
    w->tex_white = gfx_texture(1, 1, white, 0, 1);

    unsigned r = seed ? seed : 1;
    Build walls = {0}, flat = {0};
    int nb = (int)(2 * LAND_HALF / BLOCK_PITCH);                 /* 7 blocks per side */
    float start = -nb * BLOCK_PITCH * 0.5f;
    for (int bx = 0; bx < nb; bx++) for (int bz = 0; bz < nb; bz++) {
        float x0 = start + bx * BLOCK_PITCH + ROAD_W, z0 = start + bz * BLOCK_PITCH + ROAD_W;
        float x1 = start + (bx + 1) * BLOCK_PITCH, z1 = start + (bz + 1) * BLOCK_PITCH;
        float cx = (x0 + x1) * 0.5f, cz = (z0 + z1) * 0.5f;
        int centre = bx == nb / 2 && bz == nb / 2;
        if (centre) {
            /* plaza with a monument: the spawn point */
            add_box(w, v3(cx - 3, 0, cz - 3), v3(cx + 3, 1.2f, cz + 3), BOX_PROP);
            b_box(&walls, &flat, v3(cx - 3, 0, cz - 3), v3(cx + 3, 1.2f, cz + 3), 0xff9aa0a8, 0, 0);
            add_box(w, v3(cx - 1, 1.2f, cz - 1), v3(cx + 1, 14, cz + 1), BOX_PROP);
            b_box(&walls, &flat, v3(cx - 1, 1.2f, cz - 1), v3(cx + 1, 14, cz + 1), 0xffc0c8d0, 0, 0);
            for (int k = 0; k < 4; k++) {     /* planters */
                float px = cx + (k & 1 ? 11 : -11), pz = cz + (k & 2 ? 11 : -11);
                add_box(w, v3(px - 2, 0, pz - 2), v3(px + 2, 0.8f, pz + 2), BOX_PROP);
                b_box(&walls, &flat, v3(px - 2, 0, pz - 2), v3(px + 2, 0.8f, pz + 2), 0xff5a8a5a, 0, 0);
            }
            w->spawn = v3(cx, 0, cz - 8);
            flush_chunk(w, &walls, &flat, v3(cx, 7, cz), 30);
            continue;
        }
        /* split the block into 1, 2 or 4 lots */
        int split = (int)(rng_float(&r) * 3);
        int lx = split >= 1 ? 2 : 1, lz = split == 2 ? 2 : 1;
        float gap = 2.0f;
        float lw = (x1 - x0 - gap * (lx - 1)) / lx, ld = (z1 - z0 - gap * (lz - 1)) / lz;
        /* taller towards the middle of the district */
        float dist = sqrtf(cx * cx + cz * cz) / LAND_HALF;
        for (int ix = 0; ix < lx; ix++) for (int iz = 0; iz < lz; iz++) {
            float ax = x0 + ix * (lw + gap) + 1.5f, az = z0 + iz * (ld + gap) + 1.5f;
            float bxx = ax + lw - 3.0f, bzz = az + ld - 3.0f;
            float h = rng_range(&r, 10, 22) + (1.0f - dist) * rng_range(&r, 5, 45);
            h = floorf(h / 4.0f) * 4.0f;
            uint32_t col = PALETTE[rng_next(&r) % 8];
            float uoff = (float)(rng_next(&r) % 4) * 0.5f;
            add_box(w, v3(ax, 0, az), v3(bxx, h, bzz), BOX_BUILDING);
            b_box(&walls, &flat, v3(ax, 0, az), v3(bxx, h, bzz), col, 1, uoff);
            /* setback tower on some tall buildings */
            if (h > 30 && rng_float(&r) < 0.6f) {
                float in = 3.0f + rng_float(&r) * 2;
                float h2 = h + floorf(rng_range(&r, 8, 20) / 4) * 4;
                vec3 a2 = v3(ax + in, h, az + in), b2 = v3(bxx - in, h2, bzz - in);
                if (b2.x - a2.x > 4 && b2.z - a2.z > 4) {
                    add_box(w, a2, b2, BOX_BUILDING);
                    b_box(&walls, &flat, a2, b2, col, 1, uoff);
                    h = h2; ax = a2.x; az = a2.z; bxx = b2.x; bzz = b2.z;
                }
            }
            /* rooftop props: AC units, a water tank, an antenna */
            int nprops = 1 + (int)(rng_float(&r) * 3);
            for (int k = 0; k < nprops; k++) {
                float sx = rng_range(&r, 1.5f, 3.5f), sz = rng_range(&r, 1.5f, 3.5f), sh = rng_range(&r, 1.0f, 2.2f);
                float px = rng_range(&r, ax + 1, bxx - 1 - sx), pz = rng_range(&r, az + 1, bzz - 1 - sz);
                if (bxx - ax < sx + 3 || bzz - az < sz + 3) break;
                add_box(w, v3(px, h, pz), v3(px + sx, h + sh, pz + sz), BOX_PROP);
                b_box(&walls, &flat, v3(px, h, pz), v3(px + sx, h + sh, pz + sz), 0xffa0a4a8, 0, 0);
            }
            if (rng_float(&r) < 0.35f && bxx - ax > 8) {
                float tx = bxx - 4, tz = bzz - 4;
                b_box(&walls, &flat, v3(tx - 0.2f, h, tz - 0.2f), v3(tx + 0.2f, h + 3, tz + 0.2f), 0xff505050, 0, 0);
                add_box(w, v3(tx - 1.6f, h + 3, tz - 1.6f), v3(tx + 1.6f, h + 6.5f, tz + 1.6f), BOX_PROP);
                b_box(&walls, &flat, v3(tx - 1.6f, h + 3, tz - 1.6f), v3(tx + 1.6f, h + 6.5f, tz + 1.6f), 0xff6a5040, 0, 0);
            }
            if (rng_float(&r) < 0.25f)
                b_box(&walls, &flat, v3(ax + 2, h, az + 2), v3(ax + 2.25f, h + 9, az + 2.25f), 0xff606878, 0, 0);
        }
        /* street lamps along the block edge */
        for (int k = 0; k < 2; k++) {
            float lx_ = k ? x1 - 2 : x0 + 2, lz_ = z0 + 1;
            b_box(&walls, &flat, v3(lx_ - 0.1f, 0, lz_ - 0.1f), v3(lx_ + 0.1f, 5, lz_ + 0.1f), 0xff404448, 0, 0);
            b_box(&walls, &flat, v3(lx_ - 0.35f, 5, lz_ - 0.35f), v3(lx_ + 0.35f, 5.25f, lz_ + 0.35f), 0xff70e0ff, 0, 0);
        }
        flush_chunk(w, &walls, &flat, v3(cx, 25, cz), 45);
    }

    /* seawall around the island */
    float H = LAND_HALF;
    vec3 walls_mn[4] = {v3(-H - 1, WATER_Y - 2, -H - 1), v3(-H - 1, WATER_Y - 2, H), v3(-H - 1, WATER_Y - 2, -H), v3(H, WATER_Y - 2, -H)};
    vec3 walls_mx[4] = {v3(H + 1, 0.8f, -H), v3(H + 1, 0.8f, H + 1), v3(-H, 0.8f, H), v3(H + 1, 0.8f, H)};
    for (int k = 0; k < 4; k++) {
        add_box(w, walls_mn[k], walls_mx[k], BOX_SEAWALL);
        b_box(&walls, &flat, walls_mn[k], walls_mx[k], 0xff8890a0, 0, 0);
        vec3 c = v3lerp(walls_mn[k], walls_mx[k], 0.5f);
        flush_chunk(w, &walls, &flat, c, 2 * H);
    }

    /* ground: one quad per block pitch so texture and fog stay well behaved */
    b_reset(&flat);
    for (int gx = 0; gx < nb; gx++) for (int gz = 0; gz < nb; gz++) {
        float x0 = start + gx * BLOCK_PITCH, z0 = start + gz * BLOCK_PITCH;
        float x1 = x0 + BLOCK_PITCH, z1 = z0 + BLOCK_PITCH;
        b_quad(&flat, v3(x0, 0, z1), v3(x1, 0, z1), v3(x1, 0, z0), v3(x0, 0, z0), 0, 0, 1, 1, 0xffffffff);
    }
    /* the land slab extends to the seawall: fill the strip outside the grid */
    float e = -start;
    if (H > e) {
        b_quad(&flat, v3(-H, 0, -e), v3(H, 0, -e), v3(H, 0, -H), v3(-H, 0, -H), 0, 0, 1, 1, 0xff9a9a9a);
        b_quad(&flat, v3(-H, 0, H), v3(H, 0, H), v3(H, 0, e), v3(-H, 0, e), 0, 0, 1, 1, 0xff9a9a9a);
        b_quad(&flat, v3(-H, 0, e), v3(-e, 0, e), v3(-e, 0, -e), v3(-H, 0, -e), 0, 0, 1, 1, 0xff9a9a9a);
        b_quad(&flat, v3(e, 0, e), v3(H, 0, e), v3(H, 0, -e), v3(e, 0, -e), 0, 0, 1, 1, 0xff9a9a9a);
    }
    /* UVs for ground: world position / pitch, offset so roads line up */
    for (int i = 0; i < flat.nv; i++) {
        flat.v[i].u = (flat.v[i].x - start) / BLOCK_PITCH;
        flat.v[i].v = (flat.v[i].z - start) / BLOCK_PITCH;
    }
    w->ground_mesh = gfx_mesh(flat.v, flat.nv, flat.i, flat.ni);

    /* water: big ring of quads around the island */
    b_reset(&flat);
    float W = 900;
    for (int i = -3; i < 3; i++) for (int j = -3; j < 3; j++) {
        float x0 = i * 300.0f, z0 = j * 300.0f, x1 = x0 + 300, z1 = z0 + 300;
        b_quad(&flat, v3(x0, WATER_Y, z1), v3(x1, WATER_Y, z1), v3(x1, WATER_Y, z0), v3(x0, WATER_Y, z0),
               x0 / 12, z0 / 12, x1 / 12, z1 / 12, 0xffffffff);
    }
    (void)W;
    w->water_mesh = gfx_mesh(flat.v, flat.nv, flat.i, flat.ni);
    free(walls.v); free(walls.i); free(flat.v); free(flat.i);
}

void world_draw(World *w, const mat4 *proj, const mat4 *view, vec3 cam, vec3 fwd, float fog_end) {
    gfx_set_matrices(proj, view);
    gfx_draw_mesh(w->water_mesh, w->tex_water, 0);
    gfx_draw_mesh(w->ground_mesh, w->tex_ground, 0);
    for (int i = 0; i < w->nchunk; i++) {
        Chunk *c = &w->chunk[i];
        vec3 d = v3sub(c->center, cam);
        float dist = v3len(d);
        if (dist - c->radius > fog_end) continue;
        if (dist > c->radius && v3dot(d, fwd) < -c->radius) continue;    /* behind the camera */
        if (c->mesh_tex) gfx_draw_mesh(c->mesh_tex, w->tex_facade, 0);
        if (c->mesh_flat) gfx_draw_mesh(c->mesh_flat, w->tex_white, 0);
    }
}

int world_on_land(float x, float z) {
    return fabsf(x) < LAND_HALF && fabsf(z) < LAND_HALF;
}

int world_query(const World *w, vec3 mn, vec3 mx, int *out, int max) {
    int n = 0;
    int gx0 = (int)floorf((mn.x + LAND_HALF) / GRID_CELL), gx1 = (int)floorf((mx.x + LAND_HALF) / GRID_CELL);
    int gz0 = (int)floorf((mn.z + LAND_HALF) / GRID_CELL), gz1 = (int)floorf((mx.z + LAND_HALF) / GRID_CELL);
    if (gx0 < 0) gx0 = 0; if (gz0 < 0) gz0 = 0; if (gx1 >= GRID_N) gx1 = GRID_N - 1; if (gz1 >= GRID_N) gz1 = GRID_N - 1;
    for (int gx = gx0; gx <= gx1; gx++) for (int gz = gz0; gz <= gz1; gz++)
        for (int k = 0; k < w->ncell[gx][gz]; k++) {
            int id = w->cell[gx][gz][k];
            const Box *b = &w->box[id];
            if (b->mx.x < mn.x || b->mn.x > mx.x || b->mx.y < mn.y || b->mn.y > mx.y || b->mx.z < mn.z || b->mn.z > mx.z) continue;
            int dup = 0;
            for (int q = 0; q < n; q++) if (out[q] == id) { dup = 1; break; }
            if (!dup && n < max) out[n++] = id;
        }
    return n;
}

float world_floor(const World *w, float x, float z, float y, float radius) {
    float best = world_on_land(x, z) ? 0.0f : -1000.0f;
    int ids[64];
    int n = world_query(w, v3(x - radius, -10, z - radius), v3(x + radius, y + 0.01f, z + radius), ids, 64);
    for (int i = 0; i < n; i++) {
        const Box *b = &w->box[ids[i]];
        if (b->mx.y <= y + 0.01f && b->mx.y > best &&
            x > b->mn.x - radius * 0.5f && x < b->mx.x + radius * 0.5f &&
            z > b->mn.z - radius * 0.5f && z < b->mx.z + radius * 0.5f)
            best = b->mx.y;
    }
    return best;
}

int world_raycast(const World *w, vec3 a, vec3 b, float *tout, vec3 *nout) {
    vec3 mn = v3(fminf(a.x, b.x), fminf(a.y, b.y), fminf(a.z, b.z));
    vec3 mx = v3(fmaxf(a.x, b.x), fmaxf(a.y, b.y), fmaxf(a.z, b.z));
    int ids[96];
    int n = world_query(w, mn, mx, ids, 96);
    float best = 2; vec3 bn = v3(0, 1, 0);
    vec3 d = v3sub(b, a);
    for (int i = 0; i < n; i++) {
        const Box *bx = &w->box[ids[i]];
        float t0 = 0, t1 = 1; int axis = -1; float sign = 0;
        const float *pa = &a.x, *pd = &d.x, *pmn = &bx->mn.x, *pmx = &bx->mx.x;
        int ok = 1;
        for (int k = 0; k < 3; k++) {
            if (fabsf(pd[k]) < 1e-7f) { if (pa[k] < pmn[k] || pa[k] > pmx[k]) { ok = 0; break; } continue; }
            float ta = (pmn[k] - pa[k]) / pd[k], tb = (pmx[k] - pa[k]) / pd[k];
            float s = -1;
            if (ta > tb) { float tmp = ta; ta = tb; tb = tmp; s = 1; }
            if (ta > t0) { t0 = ta; axis = k; sign = s; }
            if (tb < t1) t1 = tb;
            if (t0 > t1) { ok = 0; break; }
        }
        if (ok && axis >= 0 && t0 < best) {
            best = t0;
            bn = v3(0, 0, 0); (&bn.x)[axis] = sign;
        }
    }
    /* ground plane */
    if (d.y < 0 && a.y > 0 && b.y < 0) {
        float t = a.y / -d.y;
        if (t < best && world_on_land(a.x + d.x * t, a.z + d.z * t)) { best = t; bn = v3(0, 1, 0); }
    }
    if (best <= 1) { if (tout) *tout = best; if (nout) *nout = bn; return 1; }
    return 0;
}

int world_push_circle(const World *w, vec3 *p, float r, float y0, float y1) {
    int ids[48], hit = 0;
    int n = world_query(w, v3(p->x - r - 0.5f, y0, p->z - r - 0.5f), v3(p->x + r + 0.5f, y1, p->z + r + 0.5f), ids, 48);
    for (int i = 0; i < n; i++) {
        const Box *b = &w->box[ids[i]];
        if (b->mx.y <= y0) continue;
        float cx = clampf(p->x, b->mn.x, b->mx.x), cz = clampf(p->z, b->mn.z, b->mx.z);
        float dx = p->x - cx, dz = p->z - cz, d2 = dx * dx + dz * dz;
        if (d2 < 1e-8f) {
            float pen[4] = {p->x - b->mn.x, b->mx.x - p->x, p->z - b->mn.z, b->mx.z - p->z};
            int k = 0; for (int j = 1; j < 4; j++) if (pen[j] < pen[k]) k = j;
            if (k == 0) p->x = b->mn.x - r; else if (k == 1) p->x = b->mx.x + r;
            else if (k == 2) p->z = b->mn.z - r; else p->z = b->mx.z + r;
            hit = 1;
        } else if (d2 < r * r) {
            float d = sqrtf(d2);
            p->x = cx + dx / d * r; p->z = cz + dz / d * r;
            hit = 1;
        }
    }
    return hit;
}

void world_add_checkpoints(World *w, const vec3 *pos, int n) {
    Build walls = {0}, flat = {0};
    for (int i = 0; i < n; i++) {
        vec3 c = pos[i];
        /* jersey barriers forming a broken ring */
        for (int k = 0; k < 6; k++) {
            if (k == 2 || k == 5) continue;            /* gaps to walk through */
            float a = k / 6.0f * 2 * (float)M_PI;
            float bx = c.x + cosf(a) * 7.5f, bz = c.z + sinf(a) * 7.5f;
            int along_x = fabsf(cosf(a)) < 0.6f;
            vec3 mn = v3(bx - (along_x ? 1.6f : 0.35f), 0, bz - (along_x ? 0.35f : 1.6f));
            vec3 mx = v3(bx + (along_x ? 1.6f : 0.35f), 0.95f, bz + (along_x ? 0.35f : 1.6f));
            add_box(w, mn, mx, BOX_PROP);
            b_box(&walls, &flat, mn, mx, 0xffb8b4ac, 0, 0);
            /* orange hazard stripe */
            b_box(&walls, &flat, v3(mn.x - 0.01f, 0.55f, mn.z - 0.01f), v3(mx.x + 0.01f, 0.7f, mx.z + 0.01f), 0xff2a8cf0, 0, 0);
        }
        /* floodlight mast and a supply crate stack */
        b_box(&walls, &flat, v3(c.x + 4.8f, 0, c.z + 4.8f), v3(c.x + 5.1f, 7, c.z + 5.1f), 0xff3a3e44, 0, 0);
        b_box(&walls, &flat, v3(c.x + 4.3f, 7, c.z + 4.3f), v3(c.x + 5.6f, 7.5f, c.z + 5.6f), 0xffd0f4ff, 0, 0);
        add_box(w, v3(c.x - 5.5f, 0, c.z + 3.5f), v3(c.x - 3.5f, 1.6f, c.z + 5.5f), BOX_PROP);
        b_box(&walls, &flat, v3(c.x - 5.5f, 0, c.z + 3.5f), v3(c.x - 3.5f, 1.6f, c.z + 5.5f), 0xff3c5a74, 0, 0);
        add_box(w, v3(c.x - 5.2f, 1.6f, c.z + 3.8f), v3(c.x - 3.8f, 2.8f, c.z + 5.2f), BOX_PROP);
        b_box(&walls, &flat, v3(c.x - 5.2f, 1.6f, c.z + 3.8f), v3(c.x - 3.8f, 2.8f, c.z + 5.2f), 0xff4a6a84, 0, 0);
        flush_chunk(w, &walls, &flat, v3(c.x, 3, c.z), 12);
    }
    free(walls.v); free(walls.i); free(flat.v); free(flat.i);
}
