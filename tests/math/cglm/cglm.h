#ifndef TEST_CGLM_H
#define TEST_CGLM_H
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
typedef union { int raw[2]; struct { int x, y; }; } ivec2s;
typedef union { int raw[3]; struct { int x, y, z; }; } ivec3s;
typedef union { float raw[2]; struct { float x, y; }; } vec2s;
typedef union { float raw[3]; struct { float x, y, z; }; } vec3s;
static inline vec3s glms_vec3_div(vec3s a, vec3s b) { return (vec3s){{a.x/b.x,a.y/b.y,a.z/b.z}}; }
static inline float glms_vec3_norm(vec3s a) { return sqrtf(a.x*a.x+a.y*a.y+a.z*a.z); }
#endif
