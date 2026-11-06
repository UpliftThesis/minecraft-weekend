/* Test public coordinate and storage operations without opening a GL context. */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#include <math.h>
#include <cglm/cglm.h>
#include "../../src/util/types.h"
#define UTIL_H
#define BLOCK_H
#define CHUNKMESH_H
#define PLAYER_H
#define GFX_H
#define SKY_H
#define ECS_H
#define WORLDGEN_H
#define TICKRATE 60
/* Opaque graphics/game types irrelevant to coordinate lookup and storage. */
enum BlockId { AIR = 0, STONE = 3, TORCH = 17 };
enum ChunkMeshPart { BASE, TRANSPARENT };
struct ChunkMesh;
struct ECS { int unused; };
struct Entity { int unused; };
struct Sky { int unused; };
typedef vec3s AABB[2];
static inline ivec3s glms_ivec3_add(ivec3s a, ivec3s b) { return (ivec3s){{a.x+b.x,a.y+b.y,a.z+b.z}}; }
static inline ivec3s glms_ivec3_sub(ivec3s a, ivec3s b) { return (ivec3s){{a.x-b.x,a.y-b.y,a.z-b.z}}; }
static inline ivec3s glms_ivec3_mod(ivec3s a, ivec3s b) { return (ivec3s){{a.x%b.x,a.y%b.y,a.z%b.z}}; }
#include "../../src/world/world.h"
static int checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "%s:%d: %s\n",__FILE__,__LINE__,#c); exit(1); } } while (0)
#define V(x,y,z) ((ivec3s){{x,y,z}})
#define F(x,y,z) ((vec3s){{x,y,z}})
static bool same(ivec3s a, ivec3s b) { return a.x==b.x && a.y==b.y && a.z==b.z; }
/* Omit lighting, mesh and heightmap side effects, which require a live world. */
void chunk_on_modify(struct Chunk *c, ivec3s p, u64 old, u64 value) { (void)c;(void)p;(void)old;(void)value; }
int main(void) {
    const struct { ivec3s pos, offset, local; } cases[] = {
        {V(0,0,0),V(0,0,0),V(0,0,0)},
        {V(31,31,31),V(0,0,0),V(31,31,31)},
        {V(32,64,96),V(1,2,3),V(0,0,0)},
        {V(-1,-32,-33),V(-1,-1,-2),V(31,0,31)},
        {V(-32,32,-64),V(-1,1,-2),V(0,0,0)},
        {V(-33,33,-65),V(-2,1,-3),V(31,1,31)}
    };
    for (size_t i=0; i<sizeof cases/sizeof cases[0]; ++i) {
        CHECK(same(world_pos_to_offset(cases[i].pos),cases[i].offset));
        CHECK(same(world_pos_to_chunk_pos(cases[i].pos),cases[i].local));
    }
    CHECK(same(world_pos_to_block(F(-0.01f,31.99f,-32.f)),V(-1,31,-32)));
    struct Chunk chunk = {0}; chunk.data = calloc(CHUNK_VOLUME,sizeof *chunk.data); CHECK(chunk.data != NULL);
    struct Chunk *chunks[8] = {0};
    struct World world = {0}; world.chunks_size=2; world.chunks_origin=V(-1,0,-1); world.chunks=chunks;
    ivec3s offset=V(-1,0,-1); chunks[world_chunk_index(&world,offset)] = &chunk;
    for (size_t i=0; i<8; ++i) CHECK(world_chunk_index(&world,world_chunk_offset(&world,i))==i);
    CHECK(world_contains(&world,V(-1,0,-1)));
    CHECK(!world_contains(&world,V(0,0,0)));
    CHECK(world_in_bounds(&world,V(0,0,0)));
    CHECK(!world_in_bounds(&world,V(32,0,0)));
    ivec3s p=V(-1,0,-1), neighbor=V(-2,0,-1);
    world_set_block(&world,p,STONE);
    world_set_torchlight(&world,p,0xA53F);
    world_set_sunlight(&world,p,11);
    CHECK(world_get_block(&world,p)==STONE);
    CHECK(world_get_torchlight(&world,p)==0xA53F);
    CHECK(world_get_sunlight(&world,p)==11);
    CHECK(world_get_light(&world,p)==0xBA53F);
    CHECK(world_get_block(&world,neighbor)==AIR);
    world_set_block(&world,p,TORCH);
    CHECK(world_get_torchlight(&world,p)==0xA53F && world_get_sunlight(&world,p)==11);
    world_set_torchlight(&world,p,0);
    CHECK(world_get_block(&world,p)==TORCH && world_get_torchlight(&world,p)==0);
    world_set_sunlight(&world,p,7);
    CHECK(world_get_block(&world,p)==TORCH && world_get_sunlight(&world,p)==7);
    world_set_block(&world,V(0,0,0),STONE);
    CHECK(world_get_block(&world,V(0,0,0))==AIR);
    free(chunk.data);
    printf("world: %d checks passed\n", checks);
    return 0;
}
