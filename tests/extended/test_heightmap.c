/* Regression tests for the world heightmap: for every (x, z) column,
 * world_heightmap_get returns the y of the highest non-transparent block in
 * the loaded world, or HEIGHTMAP_UNKNOWN if the column has none.
 *
 * The oracle is an independent top-down scan over the loaded region using an
 * explicit list of which blocks are see-through (taken from src/block/ sources).
 */
#include "check.h"
#include "harness.h"

static ivec3s P(int x, int y, int z) { return (ivec3s){{ x, y, z }}; }
static ivec2s XZ(int x, int z) { return (ivec2s){{ x, z }}; }

/* Blocks marked transparent in src/block/ sources (they never count for the heightmap). */
static bool see_through(enum BlockId b) {
    switch (b) {
    case AIR: case WATER: case GLASS: case LEAVES: case ROSE: case BUTTERCUP: case LAVA:
    case TORCH: case SHRUB: case TALLGRASS: case PINE_LEAVES:
        return true;
    default:
        return false;
    }
}

/* Loaded bounds of a world created with tw_create(center, chunks, ...). */
typedef struct { ivec3s lo; int n; } Bounds;

static Bounds bounds_of(ivec3s center, int chunks) {
    ivec3s c = world_pos_to_offset(center);
    return (Bounds){ P((c.x - chunks / 2) * CHUNK_SIZE_X, (c.y - chunks / 2) * CHUNK_SIZE_Y,
                       (c.z - chunks / 2) * CHUNK_SIZE_Z), chunks * CHUNK_SIZE_X };
}

static s64 oracle_height(struct World *w, Bounds b, int x, int z) {
    for (int y = b.lo.y + b.n - 1; y >= b.lo.y; y--)
        if (!see_through(world_get_block(w, P(x, y, z)))) return y;
    return HEIGHTMAP_UNKNOWN;
}

static void check_all_columns(struct World *w, Bounds b, const char *ctx) {
    for (int x = b.lo.x; x < b.lo.x + b.n; x++)
        for (int z = b.lo.z; z < b.lo.z + b.n; z++) {
            s64 got = world_heightmap_get(w, XZ(x, z)), want = oracle_height(w, b, x, z);
            CHECK_MSG(got == want, "%s: heightmap at (%d,%d) is %lld, expected %lld",
                      ctx, x, z, (long long)got, (long long)want);
        }
}

static struct World *empty_world(ivec3s center, int chunks) {
    struct World *w = tw_create(center, chunks, 1);
    tw_fill_empty(w);
    return w;
}

TEST(heightmap_empty_world_is_unknown_everywhere) {
    ivec3s c = P(0, 0, 0);
    struct World *w = empty_world(c, 2);
    Bounds b = bounds_of(c, 2);
    for (int x = b.lo.x; x < b.lo.x + b.n; x++)
        for (int z = b.lo.z; z < b.lo.z + b.n; z++)
            CHECK_MSG(world_heightmap_get(w, XZ(x, z)) == HEIGHTMAP_UNKNOWN, "(%d,%d) known", x, z);
    tw_destroy(w);
}

TEST(heightmap_unknown_before_any_chunk_is_loaded) {
    struct World *w = tw_create(P(0, 0, 0), 2, 1);
    CHECK(world_heightmap_get(w, XZ(0, 0)) == HEIGHTMAP_UNKNOWN);
    CHECK(world_heightmap_get(w, XZ(-1, -1)) == HEIGHTMAP_UNKNOWN);
    tw_destroy(w);
}

TEST(heightmap_placing_solid_block_raises_it_only_upwards) {
    struct World *w = empty_world(P(0, 0, 0), 2);
    world_set_block(w, P(3, -5, 7), STONE);
    CHECK_EQ(world_heightmap_get(w, XZ(3, 7)), -5);
    world_set_block(w, P(3, 10, 7), DIRT);
    CHECK_EQ(world_heightmap_get(w, XZ(3, 7)), 10);
    world_set_block(w, P(3, 2, 7), STONE); /* below the top: no change */
    CHECK_EQ(world_heightmap_get(w, XZ(3, 7)), 10);
    world_set_block(w, P(3, 10, 7), PLANKS); /* replace top solid by another solid */
    CHECK_EQ(world_heightmap_get(w, XZ(3, 7)), 10);
    /* neighbouring columns untouched */
    CHECK(world_heightmap_get(w, XZ(2, 7)) == HEIGHTMAP_UNKNOWN);
    CHECK(world_heightmap_get(w, XZ(3, 8)) == HEIGHTMAP_UNKNOWN);
    CHECK(world_heightmap_get(w, XZ(4, 7)) == HEIGHTMAP_UNKNOWN);
    CHECK(world_heightmap_get(w, XZ(3, 6)) == HEIGHTMAP_UNKNOWN);
    tw_destroy(w);
}

TEST(heightmap_ignores_transparent_blocks) {
    static const enum BlockId tr[] = { GLASS, TORCH, LEAVES, WATER, ROSE, TALLGRASS, PINE_LEAVES, SHRUB, BUTTERCUP };
    struct World *w = empty_world(P(16, 16, 16), 1);
    for (size_t i = 0; i < sizeof tr / sizeof tr[0]; i++) {
        int x = (int)i;
        world_set_block(w, P(x, 20, 4), tr[i]);
        CHECK_MSG(world_heightmap_get(w, XZ(x, 4)) == HEIGHTMAP_UNKNOWN, "block %d counted", tr[i]);
        world_set_block(w, P(x, 5, 4), STONE);
        world_set_block(w, P(x, 25, 4), tr[i]);
        CHECK_MSG(world_heightmap_get(w, XZ(x, 4)) == 5, "block %d counted above stone", tr[i]);
    }
    tw_destroy(w);
}

TEST(heightmap_removing_top_falls_back_to_next_solid_then_unknown) {
    /* 2x2x2 world: y in [-32, 31], chunk border between y=-1 and y=0 */
    struct World *w = empty_world(P(0, 0, 0), 2);
    ivec2s col = XZ(-1, 0);
    world_set_block(w, P(-1, -20, 0), STONE);
    world_set_block(w, P(-1, 5, 0), STONE);
    world_set_block(w, P(-1, 10, 0), STONE);
    world_set_block(w, P(-1, 14, 0), GLASS);
    CHECK_EQ(world_heightmap_get(w, col), 10);
    world_set_block(w, P(-1, 10, 0), AIR);
    CHECK_EQ(world_heightmap_get(w, col), 5);
    world_set_block(w, P(-1, 5, 0), GLASS); /* replaced by a transparent block */
    CHECK_EQ(world_heightmap_get(w, col), -20); /* in the lower chunk */
    world_set_block(w, P(-1, -20, 0), AIR);
    CHECK(world_heightmap_get(w, col) == HEIGHTMAP_UNKNOWN);
    /* removing something below the top does not change it */
    world_set_block(w, P(-1, 0, 0), STONE);
    world_set_block(w, P(-1, -1, 0), STONE);
    world_set_block(w, P(-1, -1, 0), AIR);
    CHECK_EQ(world_heightmap_get(w, col), 0);
    tw_destroy(w);
}

TEST(heightmap_negative_coordinates_and_chunk_boundaries) {
    ivec3s c = P(0, 0, 0);
    struct World *w = empty_world(c, 2);
    Bounds b = bounds_of(c, 2);
    static const int xs[] = { -32, -31, -1, 0, 1, 31 };
    static const int ys[] = { -32, -1, 0, 31 };
    int k = 0;
    for (size_t i = 0; i < 6; i++)
        for (size_t j = 0; j < 6; j++) {
            int y = ys[k++ % 4];
            world_set_block(w, P(xs[i], y, xs[j]), STONE);
            CHECK_EQ(world_heightmap_get(w, XZ(xs[i], xs[j])), y);
        }
    check_all_columns(w, b, "boundary columns");
    /* the topmost and bottommost loaded layers */
    world_set_block(w, P(-32, 31, 31), STONE);
    CHECK_EQ(world_heightmap_get(w, XZ(-32, 31)), 31);
    world_set_block(w, P(-32, 31, 31), AIR);
    check_all_columns(w, b, "after removing top-layer block");
    tw_destroy(w);
}

TEST(heightmap_computed_at_chunk_load_matches_blocks) {
    test_rng_seed(1234);
    ivec3s c = P(16, 16, 16);
    struct World *w = tw_create(c, 1, 1);
    struct TWBlock *bl = malloc(2002 * sizeof *bl);
    size_t n = 0;
    static const enum BlockId kinds[] = { STONE, DIRT, GLASS, LEAVES, WATER, TORCH, SAND };
    for (int i = 0; i < 2000; i++) {
        ivec3s p = P((int)test_rng_range(0, 31), (int)test_rng_range(0, 31), (int)test_rng_range(0, 31));
        bl[n++] = (struct TWBlock){ p, kinds[test_rng_range(0, 6)] };
    }
    /* a few hand-made columns */
    bl[n++] = (struct TWBlock){ {{ 0, 0, 0 }}, STONE };   /* bottom layer */
    bl[n++] = (struct TWBlock){ {{ 31, 31, 31 }}, STONE }; /* top layer */
    tw_load_prefilled(w, world_pos_to_offset(c), bl, n);
    free(bl);
    check_all_columns(w, bounds_of(c, 1), "prefilled chunk");
    CHECK_EQ(world_heightmap_get(w, XZ(31, 31)), 31);
    tw_destroy(w);
}

TEST(heightmap_negative_chunk_at_load_matches_blocks) {
    ivec3s c = P(-40, -40, -40); /* chunk offset (-2,-2,-2), blocks -64..-33 */
    struct World *w = tw_create(c, 1, 1);
    struct TWBlock bl[] = {
        { {{ 0, 3, 0 }}, STONE }, { {{ 0, 7, 0 }}, GLASS },
        { {{ 31, 30, 0 }}, DIRT }, { {{ 5, 0, 31 }}, STONE }, { {{ 6, 31, 9 }}, LEAVES },
    };
    tw_load_prefilled(w, world_pos_to_offset(c), bl, sizeof bl / sizeof bl[0]);
    CHECK_EQ(world_heightmap_get(w, XZ(-64, -64)), -64 + 3);
    CHECK_EQ(world_heightmap_get(w, XZ(-33, -64)), -64 + 30);
    CHECK_EQ(world_heightmap_get(w, XZ(-59, -33)), -64);
    CHECK(world_heightmap_get(w, XZ(-58, -55)) == HEIGHTMAP_UNKNOWN);
    check_all_columns(w, bounds_of(c, 1), "negative prefilled chunk");
    tw_destroy(w);
}

/* Two vertically stacked chunks loaded in either order give the highest block. */
static void stacked(bool upper_first) {
    ivec3s c = P(0, 0, 0); /* chunks_size 2: chunk offsets -1..0 */
    struct World *w = tw_create(c, 2, 1);
    struct TWBlock upper[] = { { {{ 4, 3, 4 }}, STONE }, { {{ 9, 9, 9 }}, GLASS } };
    struct TWBlock lower[] = { { {{ 4, 20, 4 }}, STONE }, { {{ 6, 20, 6 }}, STONE }, { {{ 9, 2, 9 }}, DIRT } };
    if (upper_first) {
        tw_load_prefilled(w, P(0, 0, 0), upper, 2);
        tw_load_prefilled(w, P(0, -1, 0), lower, 3);
    } else {
        tw_load_prefilled(w, P(0, -1, 0), lower, 3);
        tw_load_prefilled(w, P(0, 0, 0), upper, 2);
    }
    CHECK_EQ(world_heightmap_get(w, XZ(4, 4)), 3);
    CHECK_EQ(world_heightmap_get(w, XZ(6, 6)), -32 + 20);
    CHECK_EQ(world_heightmap_get(w, XZ(9, 9)), -32 + 2);
    CHECK(world_heightmap_get(w, XZ(5, 5)) == HEIGHTMAP_UNKNOWN);
    tw_fill_empty(w);
    check_all_columns(w, bounds_of(c, 2), upper_first ? "upper first" : "lower first");
    /* edits after loading: removing the upper block falls back into the lower chunk */
    world_set_block(w, P(4, 3, 4), AIR);
    CHECK_EQ(world_heightmap_get(w, XZ(4, 4)), -32 + 20);
    tw_destroy(w);
}

TEST(heightmap_stacked_chunks_upper_loaded_first) { stacked(true); }
TEST(heightmap_stacked_chunks_lower_loaded_first) { stacked(false); }

TEST(heightmap_matches_blocks_under_random_edits) {
    test_rng_seed(777);
    ivec3s c = P(0, 0, 0);
    struct World *w = empty_world(c, 2);
    Bounds b = bounds_of(c, 2);
    static const enum BlockId kinds[] = { STONE, AIR, AIR, GLASS, TORCH, DIRT, LEAVES };
    for (int step = 1; step <= 1500; step++) {
        ivec3s p = P((int)test_rng_range(-4, 3), (int)test_rng_range(-32, 31), (int)test_rng_range(-4, 3));
        world_set_block(w, p, kinds[test_rng_range(0, 6)]);
        if (step % 100 == 0) {
            char ctx[48];
            snprintf(ctx, sizeof ctx, "after %d edits", step);
            check_all_columns(w, b, ctx);
        }
    }
    tw_destroy(w);
}

/* Falling back must also find a block on the very lowest loaded layer
 * (added after a mutation probe showed this boundary was untested). */
TEST(heightmap_fallback_reaches_lowest_loaded_layer) {
    struct World *w = empty_world(P(0, 0, 0), 2); /* loaded y range: -32 .. 31 */
    world_set_block(w, P(3, -32, 4), STONE);
    world_set_block(w, P(3, 20, 4), STONE);
    CHECK_EQ(world_heightmap_get(w, XZ(3, 4)), 20);
    world_set_block(w, P(3, 20, 4), AIR);
    CHECK_EQ(world_heightmap_get(w, XZ(3, 4)), -32);
    world_set_block(w, P(3, -32, 4), AIR);
    CHECK(world_heightmap_get(w, XZ(3, 4)) == HEIGHTMAP_UNKNOWN);
    tw_destroy(w);
}

int main(int argc, char **argv) { return run_tests(argc, argv); }
