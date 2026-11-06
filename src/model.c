#include <stdlib.h>
#include <string.h>
#include "model.h"
#include "platform.h"

static uint32_t rd32(const uint8_t **p) { uint32_t v; memcpy(&v, *p, 4); *p += 4; return v; }
static float rdf(const uint8_t **p) { float v; memcpy(&v, *p, 4); *p += 4; return v; }

int model_load(Model *m, const char *file) {
    int size;
    uint8_t *data = plat_load(file, &size);
    if (!data || memcmp(data, "NMDL", 4)) { free(data); return 0; }
    const uint8_t *p = data + 4;
    memset(m, 0, sizeof *m);
    rd32(&p); /* version */
    m->nverts = rd32(&p); m->nidx = rd32(&p); m->nsub = rd32(&p); m->nbones = rd32(&p);
    m->height = rdf(&p);
    if (m->nbones > MAX_BONES || m->nsub > 8) { free(data); return 0; }
    for (int b = 0; b < m->nbones; b++) {
        memcpy(m->name[b], p, 32); p += 32;
        m->parent[b] = (int)rd32(&p);
        for (int k = 0; k < 16; k++) m->bind[b].m[k] = rdf(&p);
        for (int k = 0; k < 16; k++) m->inv_bind[b].m[k] = rdf(&p);
    }
    m->verts = malloc(sizeof(ModelVert) * m->nverts);
    memcpy(m->verts, p, sizeof(ModelVert) * m->nverts); p += sizeof(ModelVert) * m->nverts;
    for (int s = 0; s < m->nsub; s++) {
        m->sub[s].first = rd32(&p); m->sub[s].count = rd32(&p); m->sub[s].slot = rd32(&p);
    }
    m->idx = malloc(2 * m->nidx);
    memcpy(m->idx, p, 2 * m->nidx);
    free(data);

    for (int b = 0; b < m->nbones; b++) {
        int pa = m->parent[b];
        m->local[b] = pa >= 0 ? m4mul(m->inv_bind[pa], m->bind[b]) : m->bind[b];
        m->brot[b] = m4rot_only(m->bind[b]);
        m->brot_inv[b] = m4rot_inverse(m->brot[b]);
    }
    m->skinned = malloc(sizeof(GVert) * m->nverts);
    for (int i = 0; i < m->nverts; i++) {
        m->skinned[i].u = m->verts[i].uv[0];
        m->skinned[i].v = m->verts[i].uv[1];
    }
    return 1;
}

int model_bone(const Model *m, const char *name) {
    for (int b = 0; b < m->nbones; b++) if (!strcmp(m->name[b], name)) return b;
    return -1;
}

void pose_reset(Pose *p) {
    for (int b = 0; b < MAX_BONES; b++) p->used[b] = 0;
}

void pose_rotate(Pose *p, int bone, mat4 r) {
    if (bone < 0) return;
    if (!p->used[bone]) { p->rot[bone] = r; p->used[bone] = 1; }
    else p->rot[bone] = m4mul(r, p->rot[bone]);
}

static void compute_globals(const Model *m, const Pose *p, mat4 *g) {
    for (int b = 0; b < m->nbones; b++) {
        mat4 l = m->local[b];
        if (p && p->used[b]) {
            /* rotation in model axes, conjugated into the bone's bind frame */
            mat4 r = m4mul(m->brot_inv[b], m4mul(p->rot[b], m->brot[b]));
            l = m4mul(l, r);
        }
        g[b] = m->parent[b] >= 0 ? m4mul(g[m->parent[b]], l) : l;
    }
}

vec3 model_bone_pos(const Model *m, const Pose *p, int bone) {
    mat4 g[MAX_BONES];
    compute_globals(m, p, g);
    return v3(g[bone].m[12], g[bone].m[13], g[bone].m[14]);
}

void model_skin(Model *m, const Pose *p, vec3 L, float ambient, uint32_t tint) {
    mat4 g[MAX_BONES], s[MAX_BONES];
    compute_globals(m, p, g);
    for (int b = 0; b < m->nbones; b++) s[b] = m4mul(g[b], m->inv_bind[b]);
    float tr = (tint & 255) / 255.0f, tg = ((tint >> 8) & 255) / 255.0f, tb = ((tint >> 16) & 255) / 255.0f;
    for (int i = 0; i < m->nverts; i++) {
        const ModelVert *v = &m->verts[i];
        vec3 pos = v3(0, 0, 0), nrm = v3(0, 0, 0);
        vec3 vp = v3(v->pos[0], v->pos[1], v->pos[2]), vn = v3(v->nrm[0], v->nrm[1], v->nrm[2]);
        for (int k = 0; k < 4; k++) {
            if (!v->weight[k]) continue;
            float w = v->weight[k] * (1.0f / 255.0f);
            pos = v3add(pos, v3scale(m4point(s[v->bone[k]], vp), w));
            nrm = v3add(nrm, v3scale(m4dir(s[v->bone[k]], vn), w));
        }
        nrm = v3norm(nrm);
        /* soft wrap lighting keeps the painted look */
        float d = v3dot(nrm, L) * 0.5f + 0.5f;
        float l = ambient + (1.0f - ambient) * d;
        GVert *o = &m->skinned[i];
        o->x = pos.x; o->y = pos.y; o->z = pos.z;
        o->rgba = rgba32((int)(255 * l * tr), (int)(255 * l * tg), (int)(255 * l * tb), 255);
    }
}
