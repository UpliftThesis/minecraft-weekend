/* Regression tests for terrain generation (src/world/gen/), driven through
 * the game's own chunk loading (tw_load_generated / tw_fill_generated).
 *
 * No golden snapshots of exact block layouts: the tests check determinism by
 * comparing worlds with each other, and check invariants of the terrain
 * (layering, sea level, valid block ids, sky/underground) that a refactoring
 * of the generator must keep. */
#include "check.h"
#include "harness.h"

#define V3(x, y, z) ((ivec3s){{ (x), (y), (z) }})

/* sea level used by the generator: water fills everything at or below it */
#define SEA_LEVEL 0

/* ---- helpers --------------------------------------------------------- */

/* Blocks that decorations (trees, flowers, grass, shrubs) place. They can spill
 * across chunk borders, so their exact placement depends on load order. */
static bool is_decoration(enum BlockId b) {
    switch (b) {
    case LOG: case LEAVES: case PINE_LOG: case PINE_LEAVES:
    case ROSE: case BUTTERCUP: case TALLGRASS: case SHRUB:
        return true;
    default:
        return false;
    }
}

/* Blocks the terrain itself is made of (not counting AIR / WATER). */
static bool is_ground(enum BlockId b) {
    switch (b) {
    case STONE: case DIRT: case GRASS: case SAND: case SNOW: case PODZOL:
        return true;
    default:
        return false;
    }
}

/* Every block the generator may legitimately produce. */
static bool is_generated_kind(enum BlockId b) {
    return b == AIR || b == WATER || is_ground(b) || is_decoration(b);
}

/* Blocks a column's top ground block (biome surface) may be. */
static bool is_surface_kind(enum BlockId b) {
    return b == GRASS || b == SAND || b == SNOW || b == PODZOL;
}

/* range of block coordinates covered by world w (all slots) */
static void world_block_range(struct World *w, ivec3s *lo, ivec3s *hi) {
    *lo = tw_pos(w->chunks_origin, V3(0, 0, 0));
    *hi = glms_ivec3_add(*lo, V3((int)w->chunks_size * CHUNK_SIZE_X - 1,
                                  (int)w->chunks_size * CHUNK_SIZE_Y - 1,
                                  (int)w->chunks_size * CHUNK_SIZE_Z - 1));
}

/* highest ground block of column (x, z) within [y_lo, y_hi]; INT32_MIN if none */
static int ground_top(struct World *w, int x, int z, int y_lo, int y_hi) {
    for (int y = y_hi; y >= y_lo; y--)
        if (is_ground(world_get_block(w, V3(x, y, z)))) return y;
    return INT32_MIN;
}

/* A world with a single chunk slot holding the generated chunk `o`. */
static struct World *lone_chunk_world(ivec3s o, u64 seed, struct Chunk **out) {
    struct World *w = tw_create(tw_pos(o, V3(0, 0, 0)), 1, seed);
    *out = tw_load_generated(w, o);
    return w;
}

/* Generate every slot in reverse slot order (the opposite of tw_fill_generated). */
static void fill_generated_reverse(struct World *w) {
    size_t n = w->chunks_size * w->chunks_size * w->chunks_size;
    for (size_t i = n; i-- > 0;) {
        ivec3s o = world_chunk_offset(w, i);
        if (!world_get_chunk(w, o)) tw_load_generated(w, o);
    }
}

/* ---- determinism ----------------------------------------------------- */

TEST(same_seed_same_world) {
    struct World *a = tw_create(V3(0, 0, 0), 3, 42), *b = tw_create(V3(0, 0, 0), 3, 42);
    tw_fill_generated(a);
    tw_fill_generated(b);
    ivec3s lo, hi;
    world_block_range(a, &lo, &hi);
    size_t diff = 0;
    for (int x = lo.x; x <= hi.x; x++) for (int z = lo.z; z <= hi.z; z++) for (int y = lo.y; y <= hi.y; y++)
        diff += world_get_block(a, V3(x, y, z)) != world_get_block(b, V3(x, y, z));
    CHECK_EQ(diff, 0);
    tw_destroy(a);
    tw_destroy(b);
}

TEST(generation_order_does_not_change_terrain) {
    struct World *a = tw_create(V3(0, 0, 0), 3, 1234567), *b = tw_create(V3(0, 0, 0), 3, 1234567);
    tw_fill_generated(a);
    fill_generated_reverse(b);
    ivec3s lo, hi;
    world_block_range(a, &lo, &hi);
    for (int x = lo.x; x <= hi.x; x++) for (int z = lo.z; z <= hi.z; z++) for (int y = lo.y; y <= hi.y; y++) {
        enum BlockId ba = world_get_block(a, V3(x, y, z)), bb = world_get_block(b, V3(x, y, z));
        /* decorations crossing chunk borders may land differently; terrain may not */
        if (is_decoration(ba) || is_decoration(bb)) continue;
        CHECK_MSG(ba == bb, "(%d,%d,%d): %d vs %d depending on load order", x, y, z, ba, bb);
    }
    tw_destroy(a);
    tw_destroy(b);
}

TEST(chunk_terrain_independent_of_neighbours_and_world_layout) {
    /* chunk generated inside a fully generated world vs. alone in a 1-slot world */
    const u64 seed = 77;
    struct World *full = tw_create(V3(0, 0, 0), 3, seed);
    tw_fill_generated(full);
    ivec3s offsets[] = { V3(1, 0, 1), V3(-1, -1, 0), V3(0, 0, -1) };
    for (size_t k = 0; k < 3; k++) {
        struct Chunk *c;
        struct World *lone = lone_chunk_world(offsets[k], seed, &c);
        size_t compared = 0;
        chunk_foreach(p) {
            ivec3s pw = tw_pos(offsets[k], p);
            enum BlockId bf = world_get_block(full, pw), bl = chunk_get_block(c, p);
            if (is_decoration(bf) || is_decoration(bl)) continue;
            compared++;
            CHECK_MSG(bf == bl, "(%d,%d,%d): %d in full world vs %d alone", pw.x, pw.y, pw.z, bf, bl);
        }
        CHECK(compared >= CHUNK_VOLUME * 95 / 100);
        tw_destroy(lone);
    }
    tw_destroy(full);
}

TEST(regenerating_a_chunk_after_recentring_gives_same_terrain) {
    const u64 seed = 2024;
    struct World *w = tw_create(V3(0, 0, 0), 3, seed);
    tw_fill_generated(w);
    ivec3s o = V3(1, 0, 0);
    static enum BlockId first[CHUNK_VOLUME];
    { chunk_foreach(p) first[chunk_pos_to_index(p)] = world_get_block(w, tw_pos(o, p)); }

    /* move away so chunk o is dropped, then come back and regenerate it */
    world_set_center(w, V3(-5 * CHUNK_SIZE_X, 0, 0));
    CHECK(!world_get_chunk(w, o));
    world_set_center(w, V3(0, 0, 0));
    CHECK(!world_get_chunk(w, o));
    tw_load_generated(w, o);

    chunk_foreach(p) {
        enum BlockId before = first[chunk_pos_to_index(p)], after = world_get_block(w, tw_pos(o, p));
        if (is_decoration(before) || is_decoration(after)) continue;
        CHECK_MSG(before == after, "local (%d,%d,%d): %d then %d", p.x, p.y, p.z, before, after);
    }
    tw_destroy(w);
}

/* ---- variety ---------------------------------------------------------- */

/* surface heights of the 32x32 columns of chunk column (cx, cz) in [-64, 95] */
static void surface_heights(u64 seed, int cx, int cz, int out[CHUNK_SIZE_X][CHUNK_SIZE_Z]) {
    struct World *w = tw_create(tw_pos(V3(cx, 0, cz), V3(0, 0, 0)), 5, seed);
    for (int cy = -2; cy <= 2; cy++) tw_load_generated(w, V3(cx, cy, cz));
    for (int x = 0; x < CHUNK_SIZE_X; x++) for (int z = 0; z < CHUNK_SIZE_Z; z++) {
        ivec3s p = tw_pos(V3(cx, 0, cz), V3(x, 0, z));
        out[x][z] = ground_top(w, p.x, p.z, -64, 95);
    }
    tw_destroy(w);
}

static int count_differing(int a[CHUNK_SIZE_X][CHUNK_SIZE_Z], int b[CHUNK_SIZE_X][CHUNK_SIZE_Z]) {
    int n = 0;
    for (int x = 0; x < CHUNK_SIZE_X; x++) for (int z = 0; z < CHUNK_SIZE_Z; z++) n += a[x][z] != b[x][z];
    return n;
}

TEST(different_seeds_give_different_terrain) {
    static int h1[CHUNK_SIZE_X][CHUNK_SIZE_Z], h2[CHUNK_SIZE_X][CHUNK_SIZE_Z], h3[CHUNK_SIZE_X][CHUNK_SIZE_Z];
    surface_heights(1, 0, 0, h1);
    surface_heights(2, 0, 0, h2);
    surface_heights(987654321, 0, 0, h3);
    CHECK(count_differing(h1, h2) > CHUNK_SIZE_X * CHUNK_SIZE_Z / 4);
    CHECK(count_differing(h1, h3) > CHUNK_SIZE_X * CHUNK_SIZE_Z / 4);
    CHECK(count_differing(h2, h3) > CHUNK_SIZE_X * CHUNK_SIZE_Z / 4);
}

TEST(different_locations_give_different_terrain) {
    static int a[CHUNK_SIZE_X][CHUNK_SIZE_Z], b[CHUNK_SIZE_X][CHUNK_SIZE_Z];
    surface_heights(5, 0, 0, a);
    surface_heights(5, 7, -3, b);
    CHECK(count_differing(a, b) > CHUNK_SIZE_X * CHUNK_SIZE_Z / 4);
    /* and terrain is not flat within one chunk */
    int lo = INT32_MAX, hi = INT32_MIN;
    for (int x = 0; x < CHUNK_SIZE_X; x++) for (int z = 0; z < CHUNK_SIZE_Z; z++) {
        if (a[x][z] == INT32_MIN) continue;
        if (a[x][z] < lo) lo = a[x][z];
        if (a[x][z] > hi) hi = a[x][z];
    }
    int lo2 = INT32_MAX, hi2 = INT32_MIN;
    for (int x = 0; x < CHUNK_SIZE_X; x++) for (int z = 0; z < CHUNK_SIZE_Z; z++) {
        if (b[x][z] == INT32_MIN) continue;
        if (b[x][z] < lo2) lo2 = b[x][z];
        if (b[x][z] > hi2) hi2 = b[x][z];
    }
    CHECK(hi > lo || hi2 > lo2);
}

/* ---- plausibility invariants ----------------------------------------- */

TEST(deep_underground_is_solid) {
    size_t solid = 0, total = 0;
    for (u64 seed = 1; seed <= 3; seed++)
        for (int k = 0; k < 4; k++) {
            ivec3s o = V3(-30 + 20 * k, -8, 25 - 17 * k);   /* y in [-256, -225] */
            struct Chunk *c;
            struct World *w = lone_chunk_world(o, seed * 7919, &c);
            chunk_foreach(p) {
                enum BlockId b = chunk_get_block(c, p);
                CHECK_MSG(b != AIR, "air pocket at depth (%d,%d,%d)", o.x * 32 + p.x, o.y * 32 + p.y, o.z * 32 + p.z);
                solid += BLOCKS[b].solid;
                total++;
            }
            tw_destroy(w);
        }
    CHECK(solid >= total * 99 / 100);
}

TEST(high_sky_is_empty) {
    for (u64 seed = 1; seed <= 3; seed++)
        for (int k = 0; k < 4; k++) {
            ivec3s o = V3(-30 + 20 * k, 20, 25 - 17 * k);   /* y in [640, 671] */
            struct Chunk *c;
            struct World *w = lone_chunk_world(o, seed * 104729, &c);
            chunk_foreach(p) CHECK_EQ(chunk_get_block(c, p), AIR);
            tw_destroy(w);
        }
}

/* Checks layering invariants on a fully generated world; returns #columns checked. */
static void check_generated_world_invariants(u64 seed, ivec3s centre) {
    struct World *w = tw_create(centre, 3, seed);
    tw_fill_generated(w);
    ivec3s lo, hi;
    world_block_range(w, &lo, &hi);
    size_t columns = 0, with_ground = 0, typical_surface = 0;

    for (int x = lo.x; x <= hi.x; x++) for (int z = lo.z; z <= hi.z; z++) {
        columns++;
        bool any_non_air = false;
        for (int y = lo.y; y <= hi.y; y++) {
            enum BlockId b = world_get_block(w, V3(x, y, z));
            CHECK_MSG(b <= BLOCK_ID_LAST, "invalid block id %d at (%d,%d,%d)", b, x, y, z);
            CHECK_MSG(is_generated_kind(b), "unexpected block %d generated at (%d,%d,%d)", b, x, y, z);
            /* the sea is flat: no water above sea level, nothing empty below it */
            if (b == WATER) CHECK_MSG(y <= SEA_LEVEL, "water above sea level at (%d,%d,%d)", x, y, z);
            if (y <= SEA_LEVEL) CHECK_MSG(b != AIR, "air below sea level at (%d,%d,%d)", x, y, z);
            any_non_air |= b != AIR;
        }
        CHECK_MSG(any_non_air, "column (%d,%d) is empty", x, z);

        int top = ground_top(w, x, z, lo.y, hi.y);
        if (top == INT32_MIN || top == hi.y) continue;   /* deep ocean / peak beyond range */
        with_ground++;
        enum BlockId surface = world_get_block(w, V3(x, top, z));
        typical_surface += is_surface_kind(surface);
        /* above the ground there is only air, water and decorations */
        for (int y = top + 1; y <= hi.y; y++) {
            enum BlockId b = world_get_block(w, V3(x, y, z));
            CHECK_MSG(b == AIR || b == WATER || is_decoration(b),
                      "block %d floating above ground at (%d,%d,%d)", b, x, y, z);
        }
        /* a few blocks below the surface there is solid stone */
        for (int y = top - 10; y <= top - 5; y++) {
            if (y < lo.y) break;
            CHECK_MSG(world_get_block(w, V3(x, y, z)) == STONE,
                      "expected stone at (%d,%d,%d), %d below the surface", x, y, z, top - y);
        }
    }
    /* almost every column of a mid-height region has ground in it */
    CHECK_MSG(with_ground >= columns * 9 / 10, "only %zu/%zu columns have ground", with_ground, columns);
    /* surface blocks come from the biome top blocks (decorations may overwrite a few) */
    CHECK_MSG(typical_surface >= with_ground * 98 / 100, "only %zu/%zu typical surfaces",
              typical_surface, with_ground);
    tw_destroy(w);
}

TEST(generated_world_invariants_seed_42) { check_generated_world_invariants(42, V3(0, 0, 0)); }
TEST(generated_world_invariants_seed_99) { check_generated_world_invariants(99, V3(0, 0, 0)); }
TEST(generated_world_invariants_far_away) {
    check_generated_world_invariants(31337, V3(-5000, 0, 12000));
}

TEST(decorations_present_on_land) {
    /* over a large land area at least some vegetation is generated */
    struct World *w = tw_create(V3(0, 0, 0), 3, 1);
    tw_fill_generated(w);
    ivec3s lo, hi;
    world_block_range(w, &lo, &hi);
    size_t deco = 0, logs = 0;
    for (int x = lo.x; x <= hi.x; x++) for (int z = lo.z; z <= hi.z; z++) for (int y = lo.y; y <= hi.y; y++) {
        enum BlockId b = world_get_block(w, V3(x, y, z));
        deco += is_decoration(b);
        logs += b == LOG || b == PINE_LOG;
    }
    CHECK(deco > 0);
    CHECK(logs > 0);
    tw_destroy(w);
}

TEST(tree_trunks_stand_on_ground) {
    /* every column whose lowest log is directly above a non-log block stands on dirt/grass/... */
    struct World *w = tw_create(V3(0, 0, 0), 3, 1);
    tw_fill_generated(w);
    ivec3s lo, hi;
    world_block_range(w, &lo, &hi);
    size_t trunks = 0;
    for (int x = lo.x; x <= hi.x; x++) for (int z = lo.z; z <= hi.z; z++) for (int y = lo.y + 1; y <= hi.y; y++) {
        enum BlockId b = world_get_block(w, V3(x, y, z)), below = world_get_block(w, V3(x, y - 1, z));
        if ((b == LOG || b == PINE_LOG) && below != b && !is_decoration(below) && below != AIR) {
            trunks++;
            CHECK_MSG(below == GRASS || below == DIRT || below == SNOW || below == PODZOL,
                      "trunk at (%d,%d,%d) stands on %d", x, y, z, below);
        }
    }
    CHECK(trunks > 0);
    tw_destroy(w);
}

int main(int argc, char **argv) { return run_tests(argc, argv); }
