#ifndef MODEL_H
#define MODEL_H
#include <stdint.h>
#include "vmath.h"
#include "gfx.h"

#define MAX_BONES 64

typedef struct {
    float pos[3], nrm[3], uv[2];
    uint8_t bone[4], weight[4];
} ModelVert;

typedef struct { uint32_t first, count, slot; } SubMesh;

enum { SLOT_BODY, SLOT_EYE, SLOT_BROWS, SLOT_MOUTH, SLOT_PUPIL, SLOT_COUNT };

typedef struct {
    int nbones;
    char name[MAX_BONES][32];
    int parent[MAX_BONES];
    mat4 bind[MAX_BONES], inv_bind[MAX_BONES];
    mat4 local[MAX_BONES];            /* bind pose relative to parent */
    mat4 brot[MAX_BONES], brot_inv[MAX_BONES];
    int nverts, nidx, nsub;
    ModelVert *verts;
    uint16_t *idx;
    SubMesh sub[8];
    float height;
    GVert *skinned;
} Model;

/* A pose: an extra rotation for every bone, expressed in *model space axes*
 * (X = character's left, Y = up, Z = forward) and applied at the bone's head.
 * This makes code-driven animation intuitive: "swing the thigh about X". */
typedef struct {
    mat4 rot[MAX_BONES];
    int  used[MAX_BONES];
} Pose;

int  model_load(Model *m, const char *file);
int  model_bone(const Model *m, const char *name);
void pose_reset(Pose *p);
void pose_rotate(Pose *p, int bone, mat4 r);   /* post-multiplies */
/* compute skinned vertices in model space; light_dir in model space */
void model_skin(Model *m, const Pose *p, vec3 light_dir, float ambient, uint32_t tint);
/* world space position of a bone head for the given pose (model space) */
vec3 model_bone_pos(const Model *m, const Pose *p, int bone);

#endif
