#include <stdio.h>
#include <stdlib.h>
#include "../../src/util/fmath.h"

static int checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
#define V(x,y,z) ((ivec3s){{x,y,z}})
#define F(x,y,z) ((vec3s){{x,y,z}})
static int same(ivec3s a, ivec3s b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
struct target { ivec3s cell; int calls; };
static bool occupied(void *context, ivec3s p) { struct target *t=context; t->calls++; return same(t->cell,p); }
static void cast(vec3s start, vec3s direction, float distance, ivec3s cell, bool expected, enum Direction face) {
    struct target t = {cell,0};
    ivec3s out = V(999,999,999);
    enum Direction got = NORTH;
    bool hit = ray_block((struct Ray){start,direction}, distance, &t, occupied, &out, &got);
    CHECK(hit == expected);
    CHECK(t.calls > 0);
    if (hit) { CHECK(same(out,cell)); CHECK(got == face); }
}
int main(void) {
    const ivec3s directions[] = {V(0,0,-1),V(0,0,1),V(1,0,0),V(-1,0,0),V(0,1,0),V(0,-1,0)};
    for (int i=0; i<6; ++i) {
        CHECK(same(DIR2IVEC3S(i),directions[i]));
        CHECK(IVEC3S2DIR(directions[i]) == (enum Direction)i);
        vec3s f = DIR2VEC3S(i);
        CHECK(f.x==directions[i].x && f.y==directions[i].y && f.z==directions[i].z);
    }
    cast(F(0.5f,0.5f,0.5f), F(1,0,0), 3, V(2,0,0), true, WEST);
    cast(F(0.5f,0.5f,0.5f), F(-1,0,0), 3, V(-2,0,0), true, EAST);
    cast(F(-0.5f,-0.5f,-0.5f), F(0,1,0), 2, V(-1,0,-1), true, DOWN);
    cast(F(0.5f,0.5f,0.5f), F(0,-1,0), 2, V(0,-1,0), true, UP);
    cast(F(0.5f,0.5f,0.5f), F(0,0,1), 2, V(0,0,1), true, NORTH);
    cast(F(0.5f,0.5f,0.5f), F(0,0,-1), 2, V(0,0,-1), true, SOUTH);
    cast(F(0.5f,0.5f,0.5f), F(1,0,0), 1.49f, V(2,0,0), false, NORTH);
    cast(F(0.5f,0.5f,0.5f), F(1,0,0), 1.5f, V(2,0,0), true, WEST);
    cast(F(0.5f,0.5f,0.5f), F(2,0,0), 1.5f, V(2,0,0), true, WEST);
    cast(F(0.25f,0.25f,0.25f), F(1,0.25f,0.1f), 2, V(1,0,0), true, WEST);
    struct target inside = {V(-1,2,-3),0};
    ivec3s out = V(0,0,0); enum Direction face = DOWN;
    CHECK(ray_block((struct Ray){F(-0.1f,2.5f,-2.1f),F(1,0,0)},0,&inside,occupied,&out,&face));
    CHECK(same(out,inside.cell)); CHECK(inside.calls==1);
    printf("math: %d checks passed\n", checks);
    return 0;
}
