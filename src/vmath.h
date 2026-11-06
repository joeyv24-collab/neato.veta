/* Small vector / matrix library. Matrices are column-major (OpenGL style). */
#ifndef VMATH_H
#define VMATH_H
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define DEG2RAD(x) ((x) * (float)M_PI / 180.0f)

typedef struct { float x, y, z; } vec3;
typedef struct { float m[16]; } mat4;

static inline vec3 v3(float x, float y, float z) { vec3 r = {x, y, z}; return r; }
static inline vec3 v3add(vec3 a, vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline vec3 v3sub(vec3 a, vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline vec3 v3scale(vec3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float v3dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline vec3 v3cross(vec3 a, vec3 b) {
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline float v3len(vec3 a) { return sqrtf(v3dot(a, a)); }
static inline vec3 v3norm(vec3 a) { float l = v3len(a); return l > 1e-6f ? v3scale(a, 1.0f / l) : v3(0, 0, 0); }
static inline vec3 v3lerp(vec3 a, vec3 b, float t) { return v3add(a, v3scale(v3sub(b, a), t)); }

static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float approachf(float x, float target, float step) {
    if (x < target) { x += step; if (x > target) x = target; }
    else { x -= step; if (x < target) x = target; }
    return x;
}
static inline float wrap_angle(float a) {
    while (a > (float)M_PI) a -= 2.0f * (float)M_PI;
    while (a < -(float)M_PI) a += 2.0f * (float)M_PI;
    return a;
}
static inline float smooth01(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }

static inline mat4 m4identity(void) {
    mat4 r; memset(&r, 0, sizeof r);
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}
static inline mat4 m4mul(mat4 a, mat4 b) {
    mat4 r;
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    return r;
}
static inline mat4 m4translate(vec3 t) {
    mat4 r = m4identity(); r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z; return r;
}
static inline mat4 m4scale(float s) {
    mat4 r = m4identity(); r.m[0] = r.m[5] = r.m[10] = s; return r;
}
static inline mat4 m4rotx(float a) {
    mat4 r = m4identity(); float c = cosf(a), s = sinf(a);
    r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c; return r;
}
static inline mat4 m4roty(float a) {
    mat4 r = m4identity(); float c = cosf(a), s = sinf(a);
    r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c; return r;
}
static inline mat4 m4rotz(float a) {
    mat4 r = m4identity(); float c = cosf(a), s = sinf(a);
    r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c; return r;
}
/* rotation from euler angles applied in order: Y (yaw) * X (pitch) * Z (roll) */
static inline mat4 m4euler(float x, float y, float z) {
    return m4mul(m4roty(y), m4mul(m4rotx(x), m4rotz(z)));
}
static inline vec3 m4point(mat4 m, vec3 p) {
    return v3(m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
              m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
              m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]);
}
static inline vec3 m4dir(mat4 m, vec3 p) {
    return v3(m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z,
              m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z,
              m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z);
}
static inline mat4 m4perspective(float fovy, float aspect, float zn, float zf) {
    mat4 r; memset(&r, 0, sizeof r);
    float f = 1.0f / tanf(fovy * 0.5f);
    r.m[0] = f / aspect; r.m[5] = f;
    r.m[10] = (zf + zn) / (zn - zf); r.m[11] = -1.0f;
    r.m[14] = 2.0f * zf * zn / (zn - zf);
    return r;
}
static inline mat4 m4ortho(float l, float rr, float b, float t, float n, float f) {
    mat4 r = m4identity();
    r.m[0] = 2.0f / (rr - l); r.m[5] = 2.0f / (t - b); r.m[10] = -2.0f / (f - n);
    r.m[12] = -(rr + l) / (rr - l); r.m[13] = -(t + b) / (t - b); r.m[14] = -(f + n) / (f - n);
    return r;
}
static inline mat4 m4lookat(vec3 eye, vec3 at, vec3 up) {
    vec3 f = v3norm(v3sub(at, eye));
    vec3 s = v3norm(v3cross(f, up));
    vec3 u = v3cross(s, f);
    mat4 r = m4identity();
    r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -v3dot(s, eye); r.m[13] = -v3dot(u, eye); r.m[14] = v3dot(f, eye);
    return r;
}
/* rotation part only (assumes orthonormal-ish 3x3), returns transpose */
static inline mat4 m4rot_inverse(mat4 a) {
    mat4 r = m4identity();
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) r.m[i * 4 + j] = a.m[j * 4 + i];
    return r;
}
/* normalise the 3x3 columns of a matrix and drop translation */
static inline mat4 m4rot_only(mat4 a) {
    mat4 r = m4identity();
    for (int c = 0; c < 3; c++) {
        vec3 col = v3norm(v3(a.m[c * 4], a.m[c * 4 + 1], a.m[c * 4 + 2]));
        r.m[c * 4] = col.x; r.m[c * 4 + 1] = col.y; r.m[c * 4 + 2] = col.z;
    }
    return r;
}
static inline mat4 m4axis_angle(vec3 axis, float a) {
    axis = v3norm(axis);
    float c = cosf(a), s = sinf(a), t = 1 - c;
    float x = axis.x, y = axis.y, z = axis.z;
    mat4 r = m4identity();
    r.m[0] = t * x * x + c;     r.m[4] = t * x * y - s * z; r.m[8] = t * x * z + s * y;
    r.m[1] = t * x * y + s * z; r.m[5] = t * y * y + c;     r.m[9] = t * y * z - s * x;
    r.m[2] = t * x * z - s * y; r.m[6] = t * y * z + s * x; r.m[10] = t * z * z + c;
    return r;
}

/* cheap deterministic RNG */
static inline unsigned int rng_next(unsigned int *s) {
    *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s;
}
static inline float rng_float(unsigned int *s) { return (rng_next(s) & 0xFFFFFF) / (float)0x1000000; }
static inline float rng_range(unsigned int *s, float a, float b) { return a + (b - a) * rng_float(s); }

#endif
