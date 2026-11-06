/* Regression tests for world-level chunk bookkeeping in src/world/world.c:
 * re-centring, the queue of blocks destined for not-yet-loaded chunks,
 * writes to unloaded positions and the collision-box query world_get_aabbs.
 *
 * Tests observe behaviour through the public world API (world_get_block,
 * world_get_chunk, world_heightmap_get, ...). The only internal state read is
 * the pending-block queue, through the helpers below. */
#include "check.h"
#include "harness.h"

#define V3(x, y, z) ((ivec3s){{ (x), (y), (z) }})

/* ---- helpers --------------------------------------------------------- */

/* pending-block queue accessors (kept in one place on purpose) */
static size_t pending_count(struct World *w) { return w->unloaded_blocks.size; }
static struct WorldUnloadedBlock pending_at(struct World *w, size_t i) { return w->unloaded_blocks.list[i]; }

/* number of pending entries whose position lies in chunk `offset` */
static size_t pending_in_chunk(struct World *w, ivec3s offset) {
    size_t n = 0;
    for (size_t i = 0; i < pending_count(w); i++) {
        ivec3s o = world_pos_to_offset(pending_at(w, i).pos);
        n += o.x == offset.x && o.y == offset.y && o.z == offset.z;
    }
    return n;
}

/* A recognisable block for chunk offset o, so content can be traced after moves. */
static enum BlockId marker_for(ivec3s o) {
    static const enum BlockId pool[] = { STONE, DIRT, SAND, LOG, PLANKS, COBBLESTONE, CLAY, GRAVEL, COAL };
    int k = (o.x + 4) * 7 + (o.y + 4) * 3 + (o.z + 4);
    return pool[k % (int)(sizeof pool / sizeof pool[0])];
}

/* Load every slot with a chunk holding one marker block at a fixed local position. */
static void fill_marked(struct World *w) {
    size_t n = w->chunks_size;
    for (size_t i = 0; i < n * n * n; i++) {
        ivec3s o = world_chunk_offset(w, i);
        struct TWBlock b = { V3(3, 5, 7), marker_for(o) };
        tw_load_prefilled(w, o, &b, 1);
    }
}

/* A sky chunk far above any generated terrain (terrain stays well below y=600). */
#define SKY_Y 20

/* ---- world_set_center ------------------------------------------------ */

TEST(recentre_keeps_in_range_chunks_with_content) {
    struct World *w = tw_create(V3(0, 0, 0), 3, 7);
    fill_marked(w);
    CHECK_EQ(tw_loaded_count(w), 27);

    struct Chunk *before[3][3][3];
    for (int x = -1; x <= 1; x++) for (int y = -1; y <= 1; y++) for (int z = -1; z <= 1; z++)
        before[x + 1][y + 1][z + 1] = world_get_chunk(w, V3(x, y, z));

    /* move the centre one chunk in +x */
    world_set_center(w, V3(CHUNK_SIZE_X + 4, 3, 2));

    for (int x = -1; x <= 2; x++) for (int y = -1; y <= 1; y++) for (int z = -1; z <= 1; z++) {
        ivec3s o = V3(x, y, z);
        struct Chunk *c = world_get_chunk(w, o);
        if (x == -1) {
            CHECK_MSG(c == NULL, "chunk (%d,%d,%d) is out of range but still reachable", x, y, z);
            CHECK_EQ(world_get_block(w, tw_pos(o, V3(3, 5, 7))), AIR);
        } else if (x == 2) {
            CHECK_MSG(c == NULL, "new slot (%d,%d,%d) should start unloaded", x, y, z);
        } else {
            CHECK_MSG(c == before[x + 1][y + 1][z + 1], "chunk (%d,%d,%d) not kept", x, y, z);
            CHECK_IVEC3_EQ(c->offset, o);
            CHECK_EQ(world_get_block(w, tw_pos(o, V3(3, 5, 7))), marker_for(o));
            CHECK_EQ(world_get_block(w, tw_pos(o, V3(4, 5, 7))), AIR);
        }
    }
    CHECK_EQ(tw_loaded_count(w), 18);
    tw_destroy(w);
}

TEST(recentre_diagonal_move_keeps_overlap) {
    struct World *w = tw_create(V3(0, 0, 0), 3, 7);
    fill_marked(w);
    /* move by (-1, +1, -1) chunks */
    world_set_center(w, V3(-1, CHUNK_SIZE_Y, -CHUNK_SIZE_Z));
    size_t kept = 0;
    for (int x = -2; x <= 1; x++) for (int y = -1; y <= 2; y++) for (int z = -2; z <= 1; z++) {
        ivec3s o = V3(x, y, z);
        struct Chunk *c = world_get_chunk(w, o);
        bool in_old = x >= -1 && x <= 1 && y >= -1 && y <= 1 && z >= -1 && z <= 1;
        bool in_new = x >= -2 && x <= 0 && y >= 0 && y <= 2 && z >= -2 && z <= 0;
        if (in_old && in_new) {
            CHECK_MSG(c != NULL, "overlap chunk (%d,%d,%d) lost", x, y, z);
            CHECK_EQ(world_get_block(w, tw_pos(o, V3(3, 5, 7))), marker_for(o));
            kept++;
        } else {
            CHECK_MSG(c == NULL, "chunk (%d,%d,%d) should not be loaded", x, y, z);
        }
    }
    CHECK_EQ(kept, 8);
    CHECK_EQ(tw_loaded_count(w), 8);
    tw_destroy(w);
}

TEST(recentre_far_away_drops_everything) {
    struct World *w = tw_create(V3(0, 0, 0), 3, 7);
    fill_marked(w);
    world_set_center(w, V3(10 * CHUNK_SIZE_X, 0, 0));
    CHECK_EQ(tw_loaded_count(w), 0);
    CHECK_EQ(world_get_block(w, V3(3, 5, 7)), AIR);
    /* the world is still usable: slots can be loaded again */
    tw_fill_empty(w);
    CHECK_EQ(tw_loaded_count(w), 27);
    tw_destroy(w);
}

TEST(recentre_heightmaps_follow_columns_and_new_columns_unknown) {
    struct World *w = tw_create(V3(0, 0, 0), 3, 7);
    fill_marked(w);
    /* each column's highest opaque block is the marker in its top chunk (y offset 1) */
    for (int x = -1; x <= 1; x++) for (int z = -1; z <= 1; z++) {
        ivec3s p = tw_pos(V3(x, 1, z), V3(3, 5, 7));
        CHECK_EQ(world_heightmap_get(w, (ivec2s){{ p.x, p.z }}), p.y);
    }

    world_set_center(w, V3(CHUNK_SIZE_X, 0, 0)); /* +1 chunk in x */
    for (int x = 0; x <= 2; x++) for (int z = -1; z <= 1; z++) {
        ivec3s p = tw_pos(V3(x, 1, z), V3(3, 5, 7));
        s64 h = world_heightmap_get(w, (ivec2s){{ p.x, p.z }});
        if (x == 2) {
            CHECK_EQ(h, HEIGHTMAP_UNKNOWN);
            /* every position of a new column is unknown */
            for (int lx = 0; lx < CHUNK_SIZE_X; lx += 7)
                for (int lz = 0; lz < CHUNK_SIZE_Z; lz += 5) {
                    ivec3s q = tw_pos(V3(x, 0, z), V3(lx, 0, lz));
                    CHECK_EQ(world_heightmap_get(w, (ivec2s){{ q.x, q.z }}), HEIGHTMAP_UNKNOWN);
                }
        } else {
            CHECK_EQ(h, p.y);
        }
    }
    /* a column that left the range is no longer known */
    ivec3s gone = tw_pos(V3(-1, 1, 0), V3(3, 5, 7));
    CHECK_EQ(world_heightmap_get(w, (ivec2s){{ gone.x, gone.z }}), HEIGHTMAP_UNKNOWN);
    tw_destroy(w);
}

TEST(recentre_within_same_centre_chunk_is_noop) {
    struct World *w = tw_create(V3(0, 0, 0), 3, 7);
    fill_marked(w);
    struct Chunk *before[27];
    for (size_t i = 0; i < 27; i++) before[i] = world_get_chunk(w, world_chunk_offset(w, i));
    ivec3s origin = w->chunks_origin;
    for (size_t i = 0; i < 27; i++) tw_chunk_mesh_clear_dirty(before[i]);

    world_set_center(w, V3(CHUNK_SIZE_X - 1, CHUNK_SIZE_Y - 1, 0));
    world_set_center(w, V3(17, 2, 31));

    CHECK_IVEC3_EQ(w->chunks_origin, origin);
    for (int x = -1; x <= 1; x++) for (int y = -1; y <= 1; y++) for (int z = -1; z <= 1; z++) {
        ivec3s o = V3(x, y, z);
        struct Chunk *c = world_get_chunk(w, o);
        size_t i = 0;
        for (; i < 27 && before[i] != c; i++) {}
        CHECK_MSG(i < 27, "chunk (%d,%d,%d) replaced by a no-op recentre", x, y, z);
        CHECK_IVEC3_EQ(c->offset, o);
        CHECK(!tw_chunk_mesh_dirty(c));
        CHECK_EQ(world_get_block(w, tw_pos(o, V3(3, 5, 7))), marker_for(o));
    }
    tw_destroy(w);
}

TEST(recentre_negative_coordinates_use_floor_division) {
    /* block -1 lies in chunk -1, not chunk 0 */
    struct World *w = tw_create(V3(-1, -1, -1), 3, 7);
    CHECK(world_chunk_in_bounds(w, V3(-2, -2, -2)));
    CHECK(world_chunk_in_bounds(w, V3(0, 0, 0)));
    CHECK(!world_chunk_in_bounds(w, V3(1, 0, 0)));
    CHECK(!world_chunk_in_bounds(w, V3(-3, 0, 0)));
    tw_destroy(w);
}

/* ---- writes to unloaded positions ----------------------------------- */

TEST(set_block_on_unloaded_position_is_ignored) {
    struct World *w = tw_create(V3(0, 0, 0), 3, 7);
    tw_load_empty(w, V3(0, 0, 0));
    ivec3s p = tw_pos(V3(1, 0, 0), V3(4, 4, 4));
    world_set_block(w, p, STONE);
    CHECK_EQ(world_get_block(w, p), AIR);
    /* also outside the world bounds */
    world_set_block(w, V3(1000, 1000, 1000), STONE);
    CHECK_EQ(world_get_block(w, V3(1000, 1000, 1000)), AIR);
    /* loading the chunk later does not resurrect the write */
    tw_load_empty(w, V3(1, 0, 0));
    CHECK_EQ(world_get_block(w, p), AIR);
    /* the loaded neighbour is untouched */
    CHECK_EQ(world_get_block(w, V3(4, 4, 4)), AIR);
    tw_destroy(w);
}

/* ---- pending-block queue -------------------------------------------- */

TEST(pending_block_applied_when_chunk_generated) {
    struct World *w = tw_create(V3(0, SKY_Y * CHUNK_SIZE_Y, 0), 3, 99);
    ivec3s target = V3(0, SKY_Y, 0), other = V3(1, SKY_Y, 0);
    ivec3s p = tw_pos(target, V3(10, 11, 12)), q = tw_pos(other, V3(1, 2, 3));

    world_append_unloaded_block(w, p, GLASS);
    world_append_unloaded_block(w, q, PLANKS);
    CHECK_EQ(pending_count(w), 2);
    CHECK_EQ(world_get_block(w, p), AIR); /* nothing visible before loading */

    tw_load_generated(w, target);
    CHECK_EQ(world_get_block(w, p), GLASS);
    CHECK_EQ(pending_in_chunk(w, target), 0);   /* consumed */
    CHECK_EQ(pending_in_chunk(w, other), 1);    /* untouched */
    CHECK_EQ(world_get_block(w, q), AIR);

    tw_load_generated(w, other);
    CHECK_EQ(world_get_block(w, q), PLANKS);
    CHECK_EQ(pending_in_chunk(w, other), 0);
    tw_destroy(w);
}

TEST(pending_blocks_with_negative_coordinates) {
    struct World *w = tw_create(V3(-1, SKY_Y * CHUNK_SIZE_Y - 1, -1), 3, 99);
    ivec3s target = V3(-1, SKY_Y - 1, -1);
    ivec3s p = tw_pos(target, V3(0, 31, 31));  /* = (-32, ..-1, -1) */
    world_append_unloaded_block(w, p, LOG);
    tw_load_generated(w, target);
    CHECK_EQ(world_get_block(w, p), LOG);
    CHECK_EQ(pending_count(w), 0);
    tw_destroy(w);
}

TEST(pending_queue_grows_beyond_initial_capacity) {
    struct World *w = tw_create(V3(0, SKY_Y * CHUNK_SIZE_Y, 0), 3, 99);
    ivec3s a = V3(0, SKY_Y, 0), c = V3(-1, SKY_Y, 0);
    /* 300 entries for chunk a interleaved with 300 entries for chunk c */
    enum BlockId kinds[] = { STONE, DIRT, SAND, GLASS, PLANKS };
    for (int i = 0; i < 300; i++) {
        ivec3s la = V3(i % 32, (i / 32) % 32, (i * 7) % 32);
        world_append_unloaded_block(w, tw_pos(a, la), kinds[i % 5]);
        world_append_unloaded_block(w, tw_pos(c, la), kinds[(i + 1) % 5]);
    }
    CHECK_EQ(pending_count(w), 600);
    tw_load_generated(w, a);
    for (int i = 0; i < 300; i++) {
        ivec3s la = V3(i % 32, (i / 32) % 32, (i * 7) % 32);
        CHECK_EQ(world_get_block(w, tw_pos(a, la)), kinds[i % 5]);
    }
    /* the other chunk's entries are kept, in their original order */
    CHECK_EQ(pending_count(w), 300);
    for (int i = 0; i < 300; i++) {
        ivec3s la = V3(i % 32, (i / 32) % 32, (i * 7) % 32);
        struct WorldUnloadedBlock b = pending_at(w, i);
        CHECK_IVEC3_EQ(b.pos, tw_pos(c, la));
        CHECK_EQ(b.block, kinds[(i + 1) % 5]);
    }
    tw_destroy(w);
}

TEST(remove_pending_block_preserves_order) {
    struct World *w = tw_create(V3(0, 0, 0), 3, 1);
    for (int i = 0; i < 10; i++)
        world_append_unloaded_block(w, V3(i, 100 + i, -i), (enum BlockId)(1 + i));
    world_remove_unloaded_block(w, 3);  /* middle */
    world_remove_unloaded_block(w, 0);  /* first */
    world_remove_unloaded_block(w, 7);  /* last (originally index 9) */
    int expect[] = { 1, 2, 4, 5, 6, 7, 8 };
    CHECK_EQ(pending_count(w), 7);
    for (int k = 0; k < 7; k++) {
        struct WorldUnloadedBlock b = pending_at(w, k);
        CHECK_IVEC3_EQ(b.pos, V3(expect[k], 100 + expect[k], -expect[k]));
        CHECK_EQ(b.block, 1 + expect[k]);
    }
    tw_destroy(w);
}

KNOWN_DEFECT(pending_blocks_consecutive_for_same_chunk_all_applied,
             "world_load_chunk removes a consumed entry from the pending queue while "
             "iterating with i++, so the entry that slides into slot i is skipped: of "
             "consecutive entries for the loading chunk only every other one is applied "
             "(the rest stay queued forever). Affects tree leaves that spill into "
             "chunks generated later.") {
    struct World *w = tw_create(V3(0, SKY_Y * CHUNK_SIZE_Y, 0), 3, 99);
    ivec3s target = V3(0, SKY_Y, 0);
    for (int i = 0; i < 4; i++)
        world_append_unloaded_block(w, tw_pos(target, V3(i, 1, 1)), LEAVES);
    tw_load_generated(w, target);
    for (int i = 0; i < 4; i++)
        CHECK_MSG(world_get_block(w, tw_pos(target, V3(i, 1, 1))) == LEAVES,
                  "pending block %d not applied", i);
    CHECK_EQ(pending_in_chunk(w, target), 0);
    tw_destroy(w);
}

/* ---- world_get_aabbs ------------------------------------------------- */

static bool aabb_is_unit_cube_at(AABB b, ivec3s p) {
    return b[0].x == p.x && b[0].y == p.y && b[0].z == p.z &&
           b[1].x == p.x + 1 && b[1].y == p.y + 1 && b[1].z == p.z + 1;
}

static bool solid_block(enum BlockId id) {
    switch (id) {
    case AIR: case WATER: case LAVA: case TORCH: case ROSE: case BUTTERCUP:
    case SHRUB: case TALLGRASS:
        return false;
    default:
        return true;
    }
}

TEST(block_solidity_classification) {
    harness_init();
    for (int id = 0; id <= BLOCK_ID_LAST; id++)
        CHECK_MSG(BLOCKS[id].solid == solid_block((enum BlockId)id), "block %d solidity changed", id);
}

TEST(get_aabbs_returns_solid_blocks_only) {
    struct World *w = tw_create(V3(0, 0, 0), 3, 1);
    tw_fill_empty(w);
    /* every block id once, along a line */
    for (int id = 1; id <= BLOCK_ID_LAST; id++)
        world_set_block(w, V3(id, 2, 3), (enum BlockId)id);

    AABB out[256];
    AABB area = { (vec3s){{ 0.5f, 2.25f, 3.25f }}, (vec3s){{ BLOCK_ID_LAST + 0.5f, 2.75f, 3.75f }} };
    size_t n = world_get_aabbs(w, area, out, 256);

    /* every solid block intersecting the area is reported exactly once, as its unit cube */
    size_t expected = 0;
    for (int id = 1; id <= BLOCK_ID_LAST; id++) {
        if (!solid_block((enum BlockId)id)) continue;
        expected++;
        size_t hits = 0;
        for (size_t i = 0; i < n; i++) hits += aabb_is_unit_cube_at(out[i], V3(id, 2, 3));
        CHECK_MSG(hits == 1, "solid block id %d reported %zu times", id, hits);
    }
    /* nothing but those blocks (non-solid ones excluded) */
    CHECK_EQ(n, expected);
    for (size_t i = 0; i < n; i++) {
        ivec3s p = world_pos_to_block(out[i][0]);
        CHECK(aabb_is_unit_cube_at(out[i], p));
        CHECK(solid_block(world_get_block(w, p)));
    }
    tw_destroy(w);
}

TEST(get_aabbs_covers_area_and_nothing_far_away) {
    test_rng_seed(4242);
    struct World *w = tw_create(V3(0, 0, 0), 3, 1);
    tw_fill_empty(w);
    /* random mix of solid and non-solid blocks in a region crossing chunk borders */
    enum BlockId kinds[] = { STONE, WATER, AIR, LEAVES, TORCH, GLASS, ROSE, DIRT };
    for (int x = -6; x < 6; x++) for (int y = -6; y < 6; y++) for (int z = -6; z < 6; z++)
        world_set_block(w, V3(x, y, z), kinds[test_rng_range(0, 7)]);

    for (int trial = 0; trial < 40; trial++) {
        vec3s lo = {{ test_rng_range(-400, 200) / 100.0f, test_rng_range(-400, 200) / 100.0f,
                      test_rng_range(-400, 200) / 100.0f }};
        vec3s hi = {{ lo.x + test_rng_range(10, 250) / 100.0f, lo.y + test_rng_range(10, 250) / 100.0f,
                      lo.z + test_rng_range(10, 250) / 100.0f }};
        AABB area = { lo, hi };
        AABB out[512];
        size_t n = world_get_aabbs(w, area, out, 512);

        /* (1) all solid blocks overlapping the area are present */
        for (int x = (int)floorf(lo.x); x <= (int)floorf(hi.x); x++)
            for (int y = (int)floorf(lo.y); y <= (int)floorf(hi.y); y++)
                for (int z = (int)floorf(lo.z); z <= (int)floorf(hi.z); z++) {
                    if (!solid_block(world_get_block(w, V3(x, y, z)))) continue;
                    size_t hits = 0;
                    for (size_t i = 0; i < n; i++) hits += aabb_is_unit_cube_at(out[i], V3(x, y, z));
                    CHECK_MSG(hits == 1, "trial %d: solid block (%d,%d,%d) reported %zu times",
                              trial, x, y, z, hits);
                }
        /* (2) everything returned is a solid block's unit cube near the area
         *     (a one-block margin is allowed: the query may be conservative) */
        for (size_t i = 0; i < n; i++) {
            ivec3s p = world_pos_to_block(out[i][0]);
            CHECK(aabb_is_unit_cube_at(out[i], p));
            CHECK(solid_block(world_get_block(w, p)));
            CHECK(p.x >= floorf(lo.x) - 1 && p.x <= floorf(hi.x) + 1);
            CHECK(p.y >= floorf(lo.y) - 1 && p.y <= floorf(hi.y) + 1);
            CHECK(p.z >= floorf(lo.z) - 1 && p.z <= floorf(hi.z) + 1);
        }
    }
    tw_destroy(w);
}

TEST(get_aabbs_unloaded_area_is_empty) {
    struct World *w = tw_create(V3(0, 0, 0), 3, 1);
    AABB area = { (vec3s){{ -5, -5, -5 }}, (vec3s){{ 5, 5, 5 }} };
    AABB out[8];
    CHECK_EQ(world_get_aabbs(w, area, out, 8), 0);
    tw_destroy(w);
}

KNOWN_DEFECT(get_aabbs_respects_capacity,
             "world_get_aabbs stops at capacity n with a `break` that only leaves the "
             "innermost (y) loop; the x/z loops continue and keep writing past aabbs[n-1] "
             "(buffer overflow) and the returned count exceeds n") {
    struct World *w = tw_create(V3(0, 0, 0), 3, 1);
    struct TWBlock blocks[64];
    size_t k = 0;
    for (int x = 0; x < 4; x++) for (int y = 0; y < 4; y++) for (int z = 0; z < 4; z++)
        blocks[k++] = (struct TWBlock){ V3(x, y, z), STONE };
    tw_load_prefilled(w, V3(0, 0, 0), blocks, k);

    /* oversized buffer with sentinels so an overrun is observable, not fatal */
    AABB out[256];
    for (size_t i = 0; i < 256; i++) out[i][0] = out[i][1] = (vec3s){{ -999, -999, -999 }};
    AABB area = { (vec3s){{ 0.1f, 0.1f, 0.1f }}, (vec3s){{ 2.9f, 2.9f, 2.9f }} };
    const size_t cap = 5;
    size_t n = world_get_aabbs(w, area, out, cap);
    CHECK_MSG(n <= cap, "returned %zu boxes for capacity %zu", n, cap);
    for (size_t i = cap; i < 256; i++)
        CHECK_MSG(out[i][0].x == -999, "aabbs[%zu] written beyond capacity %zu", i, cap);
    tw_destroy(w);
}

TEST(get_aabbs_exact_fit_capacity) {
    /* when the capacity equals the number of solid blocks nothing is lost */
    struct World *w = tw_create(V3(0, 0, 0), 3, 1);
    struct TWBlock b[2] = { { V3(1, 1, 1), STONE }, { V3(2, 1, 1), DIRT } };
    tw_load_prefilled(w, V3(0, 0, 0), b, 2);
    AABB out[2];
    AABB area = { (vec3s){{ 1.2f, 1.2f, 1.2f }}, (vec3s){{ 2.8f, 1.8f, 1.8f }} };
    CHECK_EQ(world_get_aabbs(w, area, out, 2), 2);
    CHECK(aabb_is_unit_cube_at(out[0], V3(1, 1, 1)) || aabb_is_unit_cube_at(out[1], V3(1, 1, 1)));
    CHECK(aabb_is_unit_cube_at(out[0], V3(2, 1, 1)) || aabb_is_unit_cube_at(out[1], V3(2, 1, 1)));
    tw_destroy(w);
}

int main(int argc, char **argv) { return run_tests(argc, argv); }
