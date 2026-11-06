/* Regression tests for block lighting (torchlight and sunlight).
 *
 * The tests only use the public world API (world_set_block, world_get_*light)
 * and compare against independent oracles:
 *   - closed-form formulas (Manhattan falloff in open air),
 *   - a breadth-first search over the block grid written in this file,
 *   - freshly built reference worlds.
 * They deliberately do not depend on how the lighting engine is implemented.
 *
 * Not asserted (see the report / other test files):
 *   - sunlight in a torch's own cell (known defect: writing torchlight clears it),
 *   - colour channels of light sources present at chunk generation (known defect),
 *   - sunlight that would have to travel down a column after having moved
 *     sideways with no horizontal path at the same level (documented quirk in
 *     light.c; a fix would legitimately change those values).
 */
#include "check.h"
#include "harness.h"

/* ---------------------------------------------------------------- helpers */

/* What TORCH emits (src/block/torch.c). */
static const int TORCH_CH[4] = { 15, 11, 5, 15 }; /* R, G, B, I */

/* Independent transparency oracle for the blocks these tests use. */
static bool see_through(enum BlockId b) {
    switch (b) {
    case AIR: case GLASS: case TORCH: case LEAVES: case WATER: return true;
    default: return false;
    }
}

static int channel(Torchlight t, int c) {
    switch (c) {
    case 0: return TORCHLIGHT_R(t);
    case 1: return TORCHLIGHT_G(t);
    case 2: return TORCHLIGHT_B(t);
    default: return TORCHLIGHT_I(t);
    }
}

static Torchlight make_tl(const int ch[4]) {
    return TORCHLIGHT_OF(ch[0], ch[1], ch[2], ch[3]);
}

static int imax(int a, int b) { return a > b ? a : b; }
static int iabs(int a) { return a < 0 ? -a : a; }

static ivec3s P(int x, int y, int z) { return (ivec3s){{ x, y, z }}; }
static bool ivec_eq(ivec3s a, ivec3s b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

/* A fully loaded, all-air test world and its block bounds. */
typedef struct {
    struct World *w;
    ivec3s lo; /* lowest loaded block position */
    int n;     /* loaded extent per axis, in blocks */
} TW;

static TW tw_empty(ivec3s center, int chunks) {
    TW t;
    t.w = tw_create(center, chunks, 1);
    tw_fill_empty(t.w);
    ivec3s c = world_pos_to_offset(center);
    t.lo = P((c.x - chunks / 2) * CHUNK_SIZE_X,
             (c.y - chunks / 2) * CHUNK_SIZE_Y,
             (c.z - chunks / 2) * CHUNK_SIZE_Z);
    t.n = chunks * CHUNK_SIZE_X;
    return t;
}

static bool tw_inside(const TW *t, ivec3s p) {
    return p.x >= t->lo.x && p.y >= t->lo.y && p.z >= t->lo.z &&
           p.x < t->lo.x + t->n && p.y < t->lo.y + t->n && p.z < t->lo.z + t->n;
}

static size_t tw_idx(const TW *t, ivec3s p) {
    return ((size_t)(p.x - t->lo.x) * t->n + (size_t)(p.y - t->lo.y)) * t->n + (size_t)(p.z - t->lo.z);
}

static size_t tw_volume(const TW *t) { return (size_t)t->n * t->n * t->n; }

#define FOR_REGION(t, p)                                                       \
    for (int p##x_ = 0; p##x_ < (t)->n; p##x_++)                               \
        for (int p##y_ = 0; p##y_ < (t)->n; p##y_++)                           \
            for (int p##z_ = 0; p##z_ < (t)->n; p##z_++)                       \
                for (ivec3s p = P((t)->lo.x + p##x_, (t)->lo.y + p##y_,        \
                                  (t)->lo.z + p##z_), *p##once_ = &p;          \
                     p##once_; p##once_ = NULL)

static const ivec3s DIRS6[6] = {
    {{ 1, 0, 0 }}, {{ -1, 0, 0 }}, {{ 0, 1, 0 }}, {{ 0, -1, 0 }}, {{ 0, 0, 1 }}, {{ 0, 0, -1 }}
};

/* Expected torchlight everywhere: for each TORCH in the loaded region, the
 * channel value minus the shortest path length through see-through blocks,
 * maximised over torches. Light does not leave the loaded region. */
static Torchlight *expected_torchlight(const TW *t) {
    size_t N = tw_volume(t);
    int *best = calloc(N * 4, sizeof(int));
    int *stamp = calloc(N, sizeof(int));
    int *dist = malloc(N * sizeof(int));
    ivec3s *queue = malloc(N * sizeof(ivec3s));
    int cur = 0;

    FOR_REGION(t, s) {
        if (world_get_block(t->w, s) != TORCH) continue;
        cur++;
        size_t head = 0, tail = 0;
        queue[tail++] = s;
        stamp[tw_idx(t, s)] = cur;
        dist[tw_idx(t, s)] = 0;
        while (head < tail) {
            ivec3s q = queue[head++];
            size_t qi = tw_idx(t, q);
            int d = dist[qi];
            for (int c = 0; c < 4; c++) best[qi * 4 + c] = imax(best[qi * 4 + c], TORCH_CH[c] - d);
            if (d >= 15) continue;
            for (int k = 0; k < 6; k++) {
                ivec3s r = glms_ivec3_add(q, DIRS6[k]);
                if (!tw_inside(t, r)) continue;
                size_t ri = tw_idx(t, r);
                if (stamp[ri] == cur) continue;
                if (!see_through(world_get_block(t->w, r))) continue;
                stamp[ri] = cur;
                dist[ri] = d + 1;
                queue[tail++] = r;
            }
        }
    }

    Torchlight *out = malloc(N * sizeof(Torchlight));
    for (size_t i = 0; i < N; i++) {
        int ch[4];
        for (int c = 0; c < 4; c++) ch[c] = imax(0, best[i * 4 + c]);
        out[i] = make_tl(ch);
    }
    free(best); free(stamp); free(dist); free(queue);
    return out;
}

/* Every loaded cell's torchlight equals the BFS oracle. */
static void check_torchlight_oracle(const TW *t, const char *ctx) {
    Torchlight *exp = expected_torchlight(t);
    FOR_REGION(t, p) {
        Torchlight got = world_get_torchlight(t->w, p), want = exp[tw_idx(t, p)];
        if (got != want) {
            free(exp);
            FAIL_("%s: torchlight at (%d,%d,%d) is %04x, expected %04x", ctx, p.x, p.y, p.z, got, want);
        }
    }
    free(exp);
}

/* Every loaded cell has the same torchlight in both worlds (same bounds). */
static void check_same_torchlight(const TW *a, const TW *b, const char *ctx) {
    FOR_REGION(a, p) {
        Torchlight x = world_get_torchlight(a->w, p), y = world_get_torchlight(b->w, p);
        CHECK_MSG(x == y, "%s: torchlight at (%d,%d,%d): %04x vs %04x", ctx, p.x, p.y, p.z, x, y);
    }
}

static void check_all_dark(const TW *t, const char *ctx) {
    FOR_REGION(t, p) {
        Torchlight got = world_get_torchlight(t->w, p);
        CHECK_MSG(got == 0, "%s: torchlight at (%d,%d,%d) is %04x, expected 0", ctx, p.x, p.y, p.z, got);
    }
}

static void set_box_shell(struct World *w, ivec3s lo, ivec3s hi, enum BlockId b) {
    for (int x = lo.x; x <= hi.x; x++)
        for (int y = lo.y; y <= hi.y; y++)
            for (int z = lo.z; z <= hi.z; z++)
                if (x == lo.x || x == hi.x || y == lo.y || y == hi.y || z == lo.z || z == hi.z)
                    world_set_block(w, P(x, y, z), b);
}

static bool in_box(ivec3s p, ivec3s lo, ivec3s hi) {
    return p.x >= lo.x && p.y >= lo.y && p.z >= lo.z && p.x <= hi.x && p.y <= hi.y && p.z <= hi.z;
}

/* Fisher-Yates shuffle of positions with the deterministic test RNG. */
static void shuffle(ivec3s *a, size_t n) {
    for (size_t i = n; i > 1; i--) {
        size_t j = (size_t)test_rng_range(0, (long long)i - 1);
        ivec3s tmp = a[i - 1]; a[i - 1] = a[j]; a[j] = tmp;
    }
}

/* ------------------------------------------------------------ torchlight */

TEST(torch_cell_has_torch_light) {
    TW t = tw_empty(P(16, 16, 16), 1);
    ivec3s s = P(16, 16, 16);
    world_set_block(t.w, s, TORCH);
    Torchlight tl = world_get_torchlight(t.w, s);
    CHECK_EQ(TORCHLIGHT_R(tl), 15);
    CHECK_EQ(TORCHLIGHT_G(tl), 11);
    CHECK_EQ(TORCHLIGHT_B(tl), 5);
    CHECK_EQ(TORCHLIGHT_I(tl), 15);
    tw_destroy(t.w);
}

TEST(torch_open_air_falloff_is_manhattan_per_channel) {
    TW t = tw_empty(P(16, 16, 16), 1);
    ivec3s s = P(15, 17, 16);
    world_set_block(t.w, s, TORCH);
    FOR_REGION(&t, p) {
        int d = iabs(p.x - s.x) + iabs(p.y - s.y) + iabs(p.z - s.z);
        Torchlight got = world_get_torchlight(t.w, p);
        for (int c = 0; c < 4; c++)
            CHECK_MSG(channel(got, c) == imax(0, TORCH_CH[c] - d),
                      "channel %d at (%d,%d,%d) d=%d: got %d", c, p.x, p.y, p.z, d, channel(got, c));
    }
    /* explicit range ends */
    CHECK_EQ(TORCHLIGHT_I(world_get_torchlight(t.w, P(s.x + 14, s.y, s.z))), 1);
    CHECK_EQ(TORCHLIGHT_I(world_get_torchlight(t.w, P(s.x + 15, s.y, s.z))), 0);
    CHECK_EQ(TORCHLIGHT_G(world_get_torchlight(t.w, P(s.x, s.y + 10, s.z))), 1);
    CHECK_EQ(TORCHLIGHT_G(world_get_torchlight(t.w, P(s.x, s.y + 11, s.z))), 0);
    CHECK_EQ(TORCHLIGHT_B(world_get_torchlight(t.w, P(s.x, s.y, s.z - 4))), 1);
    CHECK_EQ(TORCHLIGHT_B(world_get_torchlight(t.w, P(s.x, s.y, s.z - 5))), 0);
    tw_destroy(t.w);
}

TEST(torch_light_passes_through_glass_unchanged) {
    TW t = tw_empty(P(16, 16, 16), 1);
    ivec3s s = P(16, 16, 16);
    /* a glass wall and a leaves wall do not alter open-air falloff */
    for (int y = 0; y < 32; y++)
        for (int z = 0; z < 32; z++) {
            world_set_block(t.w, P(18, y, z), GLASS);
            world_set_block(t.w, P(13, y, z), LEAVES);
        }
    world_set_block(t.w, s, TORCH);
    for (int x = 0; x < 32; x++) {
        int d = iabs(x - s.x);
        CHECK_EQ(TORCHLIGHT_I(world_get_torchlight(t.w, P(x, s.y, s.z))), imax(0, 15 - d));
    }
    check_torchlight_oracle(&t, "glass/leaves walls");
    tw_destroy(t.w);
}

TEST(torch_inside_sealed_box_does_not_light_outside) {
    TW t = tw_empty(P(16, 16, 16), 1);
    ivec3s lo = P(12, 12, 12), hi = P(18, 18, 18);
    set_box_shell(t.w, lo, hi, STONE);
    ivec3s s = P(15, 15, 15);
    world_set_block(t.w, s, TORCH);
    FOR_REGION(&t, p) {
        Torchlight got = world_get_torchlight(t.w, p);
        if (!in_box(p, P(13, 13, 13), P(17, 17, 17))) {
            CHECK_MSG(got == 0, "outside/wall (%d,%d,%d) lit: %04x", p.x, p.y, p.z, got);
        } else {
            int d = iabs(p.x - s.x) + iabs(p.y - s.y) + iabs(p.z - s.z);
            CHECK_EQ(TORCHLIGHT_I(got), 15 - d);
        }
    }
    tw_destroy(t.w);
}

TEST(torch_outside_sealed_box_does_not_light_inside) {
    TW t = tw_empty(P(16, 16, 16), 1);
    world_set_block(t.w, P(10, 15, 15), TORCH);
    set_box_shell(t.w, P(12, 12, 12), P(18, 18, 18), STONE);
    for (int x = 13; x <= 17; x++)
        for (int y = 13; y <= 17; y++)
            for (int z = 13; z <= 17; z++)
                CHECK_MSG(world_get_torchlight(t.w, P(x, y, z)) == 0, "inside (%d,%d,%d) lit", x, y, z);
    check_torchlight_oracle(&t, "box built after torch");
    tw_destroy(t.w);
}

TEST(torch_light_goes_around_wall) {
    TW t = tw_empty(P(16, 16, 16), 1);
    /* wall in plane x = 16 covering the whole chunk except a 1x1 hole */
    ivec3s hole = P(16, 16, 20);
    for (int y = 0; y < 32; y++)
        for (int z = 0; z < 32; z++)
            if (!(y == hole.y && z == hole.z)) world_set_block(t.w, P(16, y, z), STONE);
    ivec3s s = P(14, 16, 16);
    world_set_block(t.w, s, TORCH);
    /* straight through would be distance 4; the path via the hole is 2+4+... */
    ivec3s target = P(18, 16, 16);
    int path = (hole.x - s.x) + iabs(hole.z - s.z) + (target.x - hole.x) + iabs(target.z - hole.z);
    CHECK_EQ(path, 12);
    CHECK_EQ(TORCHLIGHT_I(world_get_torchlight(t.w, target)), 15 - path);
    CHECK_EQ(TORCHLIGHT_R(world_get_torchlight(t.w, target)), 15 - path);
    CHECK_EQ(TORCHLIGHT_G(world_get_torchlight(t.w, target)), 0);
    check_torchlight_oracle(&t, "wall with hole");

    /* closing the hole darkens the other side completely */
    world_set_block(t.w, hole, STONE);
    for (int x = 17; x < 32; x++)
        for (int y = 0; y < 32; y++)
            for (int z = 0; z < 32; z++)
                CHECK_MSG(world_get_torchlight(t.w, P(x, y, z)) == 0, "(%d,%d,%d) lit after hole closed", x, y, z);
    check_torchlight_oracle(&t, "hole closed");

    /* opening the wall in line with the torch restores the short path */
    world_set_block(t.w, P(16, 16, 16), AIR);
    CHECK_EQ(TORCHLIGHT_I(world_get_torchlight(t.w, target)), 11);
    check_torchlight_oracle(&t, "wall opened");
    tw_destroy(t.w);
}

TEST(opaque_block_placed_in_light_blocks_and_reroutes) {
    TW t = tw_empty(P(16, 16, 16), 1);
    ivec3s s = P(16, 16, 16);
    world_set_block(t.w, s, TORCH);
    world_set_block(t.w, P(17, 16, 16), STONE);
    CHECK_EQ(world_get_torchlight(t.w, P(17, 16, 16)), 0);
    /* (18,16,16) now reached by a detour of length 4 */
    CHECK_EQ(TORCHLIGHT_I(world_get_torchlight(t.w, P(18, 16, 16))), 11);
    check_torchlight_oracle(&t, "one block");
    /* surround the torch completely */
    for (int k = 0; k < 6; k++) world_set_block(t.w, glms_ivec3_add(s, DIRS6[k]), STONE);
    FOR_REGION(&t, p) {
        if (ivec_eq(p, s)) continue;
        CHECK_MSG(world_get_torchlight(t.w, p) == 0, "(%d,%d,%d) lit while torch enclosed", p.x, p.y, p.z);
    }
    CHECK_EQ(world_get_torchlight(t.w, s), make_tl(TORCH_CH));
    tw_destroy(t.w);
}

TEST(torch_removal_restores_darkness) {
    TW t = tw_empty(P(16, 16, 16), 1);
    ivec3s s = P(10, 20, 22);
    world_set_block(t.w, s, TORCH);
    world_set_block(t.w, s, AIR);
    check_all_dark(&t, "after removal");
    /* also when replaced by an opaque block */
    world_set_block(t.w, s, TORCH);
    world_set_block(t.w, s, STONE);
    check_all_dark(&t, "after replacement by stone");
    /* and by glass */
    world_set_block(t.w, P(5, 5, 5), TORCH);
    world_set_block(t.w, P(5, 5, 5), GLASS);
    check_all_dark(&t, "after replacement by glass");
    tw_destroy(t.w);
}

TEST(removing_one_of_two_torches_matches_fresh_world) {
    static const ivec3s pairs[][2] = {
        { {{ 10, 16, 16 }}, {{ 16, 16, 16 }} }, /* overlapping light */
        { {{ 15, 16, 16 }}, {{ 16, 16, 16 }} }, /* adjacent torches */
        { {{ 4, 4, 4 }},    {{ 20, 18, 9 }} },  /* barely touching */
    };
    for (size_t i = 0; i < sizeof pairs / sizeof pairs[0]; i++) {
        for (int which = 0; which < 2; which++) {
            ivec3s gone = pairs[i][which], kept = pairs[i][1 - which];
            TW t = tw_empty(P(16, 16, 16), 1);
            world_set_block(t.w, pairs[i][0], TORCH);
            world_set_block(t.w, pairs[i][1], TORCH);
            world_set_block(t.w, gone, AIR);

            TW ref = tw_empty(P(16, 16, 16), 1);
            world_set_block(ref.w, kept, TORCH);
            check_same_torchlight(&t, &ref, "two torches minus one vs fresh");
            check_torchlight_oracle(&t, "two torches minus one");
            tw_destroy(t.w);
            tw_destroy(ref.w);
        }
    }
}

TEST(torch_placement_order_independent) {
    ivec3s a = P(-3, 2, 1), b = P(4, -2, -5), c = P(0, 6, 0);
    TW t1 = tw_empty(P(0, 0, 0), 2), t2 = tw_empty(P(0, 0, 0), 2);
    world_set_block(t1.w, P(1, 0, -2), STONE);
    world_set_block(t2.w, P(1, 0, -2), STONE);
    world_set_block(t1.w, a, TORCH); world_set_block(t1.w, b, TORCH); world_set_block(t1.w, c, TORCH);
    world_set_block(t2.w, c, TORCH); world_set_block(t2.w, b, TORCH); world_set_block(t2.w, a, TORCH);
    check_same_torchlight(&t1, &t2, "A,B,C vs C,B,A");
    check_torchlight_oracle(&t1, "three torches");
    tw_destroy(t1.w);
    tw_destroy(t2.w);
}

TEST(torch_light_crosses_chunk_borders_and_negative_coords) {
    /* 2x2x2 world spanning blocks -32..31: chunk borders at 0/-1 on every axis */
    TW t = tw_empty(P(0, 0, 0), 2);
    ivec3s s = P(0, 0, 0);
    world_set_block(t.w, s, TORCH);
    FOR_REGION(&t, p) {
        int d = iabs(p.x) + iabs(p.y) + iabs(p.z);
        Torchlight got = world_get_torchlight(t.w, p);
        for (int c = 0; c < 4; c++)
            CHECK_MSG(channel(got, c) == imax(0, TORCH_CH[c] - d),
                      "channel %d at (%d,%d,%d): %d", c, p.x, p.y, p.z, channel(got, c));
    }
    CHECK_EQ(TORCHLIGHT_I(world_get_torchlight(t.w, P(-1, -1, -1))), 12);
    world_set_block(t.w, s, AIR);
    check_all_dark(&t, "after removal across borders");
    tw_destroy(t.w);
}

TEST(light_values_stay_in_range_and_torchlight_matches_oracle_under_random_edits) {
    test_rng_seed(20240601);
    TW t = tw_empty(P(0, 0, 0), 2);
    /* edits in a box straddling the chunk borders around the origin */
    for (int step = 1; step <= 300; step++) {
        ivec3s p = P((int)test_rng_range(-6, 5), (int)test_rng_range(-6, 5), (int)test_rng_range(-6, 5));
        long long r = test_rng_range(0, 99);
        enum BlockId b = r < 35 ? STONE : r < 50 ? GLASS : r < 85 ? AIR : TORCH;
        world_set_block(t.w, p, b);
        if (step % 30 == 0) {
            char ctx[64];
            snprintf(ctx, sizeof ctx, "after %d random edits", step);
            check_torchlight_oracle(&t, ctx);
            FOR_REGION(&t, q) {
                Light l = world_get_light(t.w, q);
                CHECK(world_get_sunlight(t.w, q) <= LIGHT_MAX);
                CHECK((l >> 20) == 0); /* no bits beyond the sunlight nibble */
                for (int c = 0; c < 4; c++) CHECK(channel(world_get_torchlight(t.w, q), c) <= LIGHT_MAX);
            }
        }
    }
    tw_destroy(t.w);
}

/* -------------------------------------------------------------- sunlight */

static void check_sun_all(const TW *t, int want, const char *ctx) {
    FOR_REGION(t, p) {
        int got = world_get_sunlight(t->w, p);
        CHECK_MSG(got == want, "%s: sunlight at (%d,%d,%d) is %d, expected %d", ctx, p.x, p.y, p.z, got, want);
    }
}

TEST(sunlight_empty_world_is_full_everywhere) {
    TW t = tw_empty(P(16, 16, 16), 1);
    check_sun_all(&t, LIGHT_MAX, "1 chunk");
    tw_destroy(t.w);
    TW t2 = tw_empty(P(0, 0, 0), 2);
    check_sun_all(&t2, LIGHT_MAX, "2x2x2 chunks");
    tw_destroy(t2.w);
}

/* A single opaque block at b: full sun everywhere except below it, where the
 * column is lit sideways (one step from full-sun neighbours) and the block itself. */
static void check_single_block_sun(const TW *t, ivec3s b, const char *ctx) {
    FOR_REGION(t, p) {
        int got = world_get_sunlight(t->w, p);
        if (ivec_eq(p, b)) continue; /* opaque cell: not asserted */
        int want = (p.x == b.x && p.z == b.z && p.y < b.y) ? LIGHT_MAX - 1 : LIGHT_MAX;
        CHECK_MSG(got == want, "%s: sunlight at (%d,%d,%d) is %d, expected %d", ctx, p.x, p.y, p.z, got, want);
    }
}

TEST(sunlight_single_opaque_block_shades_its_column_placed) {
    TW t = tw_empty(P(16, 16, 16), 1);
    ivec3s b = P(10, 20, 12);
    world_set_block(t.w, b, STONE);
    check_single_block_sun(&t, b, "placed");
    world_set_block(t.w, b, AIR);
    check_sun_all(&t, LIGHT_MAX, "removed");
    tw_destroy(t.w);
}

TEST(sunlight_single_opaque_block_shades_its_column_generated) {
    struct World *w = tw_create(P(16, 16, 16), 1, 1);
    struct TWBlock blk = { {{ 10, 20, 12 }}, STONE };
    tw_load_prefilled(w, P(0, 0, 0), &blk, 1);
    TW t = { w, P(0, 0, 0), 32 };
    check_single_block_sun(&t, P(10, 20, 12), "prefilled");
    tw_destroy(w);
}

TEST(sunlight_transparent_blocks_do_not_shade) {
    TW t = tw_empty(P(16, 16, 16), 1);
    world_set_block(t.w, P(5, 25, 5), GLASS);
    world_set_block(t.w, P(6, 25, 5), LEAVES);
    world_set_block(t.w, P(7, 25, 5), WATER);
    FOR_REGION(&t, p) {
        if (p.y == 25 && p.z == 5 && p.x >= 5 && p.x <= 7) continue;
        CHECK_MSG(world_get_sunlight(t.w, p) == LIGHT_MAX, "(%d,%d,%d) shaded", p.x, p.y, p.z);
    }
    tw_destroy(t.w);
}

/* Square roof [x0..x1]x[z0..z1] at height ry: below it the sunlight is
 * 15 - (horizontal distance to the nearest uncovered column). */
static int roof_expected(ivec3s p, int x0, int x1, int z0, int z1, int ry) {
    if (p.y >= ry || p.x < x0 || p.x > x1 || p.z < z0 || p.z > z1) return LIGHT_MAX;
    int d = p.x - x0 + 1;
    if (x1 - p.x + 1 < d) d = x1 - p.x + 1;
    if (p.z - z0 + 1 < d) d = p.z - z0 + 1;
    if (z1 - p.z + 1 < d) d = z1 - p.z + 1;
    return imax(0, LIGHT_MAX - d);
}

static void check_roof(const TW *t, int x0, int x1, int z0, int z1, int ry, const char *ctx) {
    FOR_REGION(t, p) {
        if (p.y == ry && p.x >= x0 && p.x <= x1 && p.z >= z0 && p.z <= z1) continue;
        int got = world_get_sunlight(t->w, p), want = roof_expected(p, x0, x1, z0, z1, ry);
        CHECK_MSG(got == want, "%s: sunlight at (%d,%d,%d) is %d, expected %d", ctx, p.x, p.y, p.z, got, want);
    }
}

TEST(sunlight_under_roof_falls_off_from_edges_generated) {
    struct World *w = tw_create(P(16, 16, 16), 1, 1);
    struct TWBlock blocks[81];
    size_t n = 0;
    for (int x = 12; x <= 20; x++)
        for (int z = 12; z <= 20; z++) blocks[n++] = (struct TWBlock){ {{ x, 20, z }}, STONE };
    tw_load_prefilled(w, P(0, 0, 0), blocks, n);
    TW t = { w, P(0, 0, 0), 32 };
    check_roof(&t, 12, 20, 12, 20, 20, "prefilled roof");
    CHECK_EQ(world_get_sunlight(w, P(16, 19, 16)), LIGHT_MAX - 5);
    CHECK_EQ(world_get_sunlight(w, P(16, 0, 16)), LIGHT_MAX - 5);
    tw_destroy(w);
}

TEST(sunlight_under_roof_placed_block_by_block_then_removed) {
    test_rng_seed(77);
    TW t = tw_empty(P(16, 16, 16), 1);
    ivec3s cells[81];
    size_t n = 0;
    for (int x = 12; x <= 20; x++)
        for (int z = 12; z <= 20; z++) cells[n++] = P(x, 20, z);
    shuffle(cells, n);
    for (size_t i = 0; i < n; i++) world_set_block(t.w, cells[i], STONE);
    check_roof(&t, 12, 20, 12, 20, 20, "placed roof");

    /* an opening in the middle spreads sunlight sideways with falloff */
    world_set_block(t.w, P(16, 20, 16), AIR);
    FOR_REGION(&t, p) {
        if (p.y == 20 && p.x >= 12 && p.x <= 20 && p.z >= 12 && p.z <= 20 && !(p.x == 16 && p.z == 16)) continue;
        int want = roof_expected(p, 12, 20, 12, 20, 20);
        if (p.y < 20) want = imax(want, LIGHT_MAX - (iabs(p.x - 16) + iabs(p.z - 16)));
        int got = world_get_sunlight(t.w, p);
        CHECK_MSG(got == want, "hole: sunlight at (%d,%d,%d) is %d, expected %d", p.x, p.y, p.z, got, want);
    }

    /* removing the whole roof restores full sunlight */
    shuffle(cells, n);
    for (size_t i = 0; i < n; i++) world_set_block(t.w, cells[i], AIR);
    check_sun_all(&t, LIGHT_MAX, "roof removed");
    tw_destroy(t.w);
}

/* Roof over a whole 1-chunk world with a single opening: sunlight below
 * reaches 0 at Manhattan distance 15 from the opening. */
static void check_hole_roof(const TW *t, ivec3s hole, const char *ctx) {
    FOR_REGION(t, p) {
        if (p.y == hole.y && !(p.x == hole.x && p.z == hole.z)) continue;
        int want = p.y >= hole.y ? LIGHT_MAX : imax(0, LIGHT_MAX - (iabs(p.x - hole.x) + iabs(p.z - hole.z)));
        int got = world_get_sunlight(t->w, p);
        CHECK_MSG(got == want, "%s: sunlight at (%d,%d,%d) is %d, expected %d", ctx, p.x, p.y, p.z, got, want);
    }
}

TEST(sunlight_through_single_opening_generated) {
    struct World *w = tw_create(P(16, 16, 16), 1, 1);
    ivec3s hole = P(16, 24, 16);
    struct TWBlock *blocks = malloc(32 * 32 * sizeof *blocks);
    size_t n = 0;
    for (int x = 0; x < 32; x++)
        for (int z = 0; z < 32; z++)
            if (!(x == hole.x && z == hole.z)) blocks[n++] = (struct TWBlock){ {{ x, hole.y, z }}, STONE };
    tw_load_prefilled(w, P(0, 0, 0), blocks, n);
    free(blocks);
    TW t = { w, P(0, 0, 0), 32 };
    check_hole_roof(&t, hole, "prefilled");
    tw_destroy(w);
}

TEST(sunlight_through_single_opening_placed) {
    TW t = tw_empty(P(16, 16, 16), 1);
    ivec3s hole = P(16, 24, 16);
    for (int x = 0; x < 32; x++)
        for (int z = 0; z < 32; z++)
            if (!(x == hole.x && z == hole.z)) world_set_block(t.w, P(x, hole.y, z), STONE);
    check_hole_roof(&t, hole, "placed");
    /* closing the opening makes everything under the roof dark */
    world_set_block(t.w, hole, STONE);
    FOR_REGION(&t, p) {
        if (p.y >= hole.y) continue;
        CHECK_MSG(world_get_sunlight(t.w, p) == 0, "(%d,%d,%d) lit under closed roof", p.x, p.y, p.z);
    }
    /* reopening restores the pattern */
    world_set_block(t.w, hole, AIR);
    check_hole_roof(&t, hole, "reopened");
    tw_destroy(t.w);
}

static void check_box_interior_dark(struct World *w, const char *ctx) {
    for (int x = 13; x <= 17; x++)
        for (int y = 13; y <= 17; y++)
            for (int z = 13; z <= 17; z++)
                CHECK_MSG(world_get_sunlight(w, P(x, y, z)) == 0,
                          "%s: inside (%d,%d,%d) sunlight %d", ctx, x, y, z, world_get_sunlight(w, P(x, y, z)));
}

TEST(sunlight_sealed_box_interior_dark_generated) {
    struct World *w = tw_create(P(16, 16, 16), 1, 1);
    struct TWBlock blocks[7 * 7 * 7];
    size_t n = 0;
    for (int x = 12; x <= 18; x++)
        for (int y = 12; y <= 18; y++)
            for (int z = 12; z <= 18; z++)
                if (x == 12 || x == 18 || y == 12 || y == 18 || z == 12 || z == 18)
                    blocks[n++] = (struct TWBlock){ {{ x, y, z }}, STONE };
    tw_load_prefilled(w, P(0, 0, 0), blocks, n);
    check_box_interior_dark(w, "prefilled");
    CHECK_EQ(world_get_sunlight(w, P(15, 19, 15)), LIGHT_MAX);
    CHECK_EQ(world_get_sunlight(w, P(11, 15, 15)), LIGHT_MAX);
    tw_destroy(w);
}

TEST(sunlight_sealed_box_interior_dark_placed_any_order) {
    /* the last block placed (the one that seals the box) varies: lid, wall, floor */
    static const ivec3s last[] = { {{ 15, 18, 15 }}, {{ 18, 15, 15 }}, {{ 15, 12, 15 }}, {{ 12, 17, 13 }} };
    for (size_t i = 0; i < sizeof last / sizeof last[0]; i++) {
        TW t = tw_empty(P(16, 16, 16), 1);
        set_box_shell(t.w, P(12, 12, 12), P(18, 18, 18), STONE);
        world_set_block(t.w, last[i], AIR);
        CHECK(world_get_sunlight(t.w, P(15, 15, 15)) > 0); /* light gets in through the gap */
        world_set_block(t.w, last[i], STONE);
        check_box_interior_dark(t.w, "sealed");
        /* opening it again lets light in */
        world_set_block(t.w, last[i], AIR);
        CHECK(world_get_sunlight(t.w, P(15, 15, 15)) > 0);
        tw_destroy(t.w);
    }
}

TEST(torch_does_not_change_sunlight_elsewhere) {
    TW t = tw_empty(P(16, 16, 16), 1);
    ivec3s s = P(16, 10, 16);
    world_set_block(t.w, s, TORCH);
    FOR_REGION(&t, p) {
        if (ivec_eq(p, s)) continue; /* own cell: known defect elsewhere */
        CHECK_MSG(world_get_sunlight(t.w, p) == LIGHT_MAX, "(%d,%d,%d) sunlight changed", p.x, p.y, p.z);
    }
    world_set_block(t.w, s, AIR);
    check_sun_all(&t, LIGHT_MAX, "after torch removed");
    tw_destroy(t.w);
}

TEST(sunlight_incremental_edits_independent_of_order) {
    /* the same final set of blocks, placed in two different orders, gives the same sunlight */
    test_rng_seed(4242);
    enum { NB = 150 };
    ivec3s pos[NB];
    enum BlockId blk[NB];
    for (int i = 0; i < NB; i++) {
        pos[i] = P((int)test_rng_range(8, 23), (int)test_rng_range(14, 26), (int)test_rng_range(8, 23));
        blk[i] = test_rng_range(0, 3) == 0 ? GLASS : STONE;
    }
    /* deduplicate positions (keep first) */
    int m = 0;
    for (int i = 0; i < NB; i++) {
        bool dup = false;
        for (int j = 0; j < m; j++) dup |= ivec_eq(pos[j], pos[i]);
        if (!dup) { pos[m] = pos[i]; blk[m] = blk[i]; m++; }
    }
    TW a = tw_empty(P(16, 16, 16), 1), b = tw_empty(P(16, 16, 16), 1);
    for (int i = 0; i < m; i++) world_set_block(a.w, pos[i], blk[i]);
    for (int i = m - 1; i >= 0; i--) world_set_block(b.w, pos[i], blk[i]);
    FOR_REGION(&a, p) {
        int x = world_get_sunlight(a.w, p), y = world_get_sunlight(b.w, p);
        CHECK_MSG(x == y, "sunlight at (%d,%d,%d): %d vs %d", p.x, p.y, p.z, x, y);
    }
    tw_destroy(a.w);
    tw_destroy(b.w);
}

TEST(sunlight_after_random_edits_matches_freshly_generated_chunk) {
    /* incremental sunlight after many edits equals the sunlight a chunk gets
     * when it is generated with the same final blocks (no light sources) */
    test_rng_seed(99);
    TW a = tw_empty(P(16, 16, 16), 1);
    for (int i = 0; i < 400; i++) {
        ivec3s p = P((int)test_rng_range(4, 27), (int)test_rng_range(4, 27), (int)test_rng_range(4, 27));
        enum BlockId b = test_rng_range(0, 3) == 0 ? GLASS : STONE;
        if (test_rng_range(0, 3) == 0) b = AIR;
        world_set_block(a.w, p, b);
    }
    struct TWBlock *bl = malloc(32 * 32 * 32 * sizeof *bl);
    size_t m = 0;
    FOR_REGION(&a, p) {
        enum BlockId b = world_get_block(a.w, p);
        if (b != AIR) bl[m++] = (struct TWBlock){ p, b };
    }
    CHECK(m > 200);
    struct World *w = tw_create(P(16, 16, 16), 1, 1);
    tw_load_prefilled(w, P(0, 0, 0), bl, m);
    free(bl);
    FOR_REGION(&a, p) {
        int x = world_get_sunlight(a.w, p), y = world_get_sunlight(w, p);
        CHECK_MSG(x == y, "sunlight at (%d,%d,%d): incremental %d vs generated %d", p.x, p.y, p.z, x, y);
    }
    tw_destroy(a.w);
    tw_destroy(w);
}

KNOWN_DEFECT(removing_torch_next_to_lava_keeps_lava_light,
    "torchlight removal (remove_propagate) clears any neighbouring cell whose channel value is lower "
    "than the removed light, including the cell of another light source (LAVA, F/8/2/7) that was "
    "brightened by the torch; that source is never re-seeded, so the lava cell's G/B/I channels "
    "drop to 0 and lava stops emitting them") {
    TW t = tw_empty(P(16, 16, 16), 1);
    ivec3s lava = P(16, 16, 16), torch = P(17, 16, 16);
    world_set_block(t.w, lava, LAVA);
    world_set_block(t.w, torch, TORCH);
    world_set_block(t.w, torch, AIR);

    TW ref = tw_empty(P(16, 16, 16), 1);
    world_set_block(ref.w, lava, LAVA);
    CHECK_EQ(world_get_torchlight(ref.w, lava), TORCHLIGHT_OF(15, 8, 2, 7)); /* sanity of the oracle */
    CHECK_EQ(world_get_torchlight(t.w, lava), TORCHLIGHT_OF(15, 8, 2, 7));
    check_same_torchlight(&t, &ref, "lava after neighbouring torch removed vs lava alone");
    tw_destroy(t.w);
    tw_destroy(ref.w);
}

int main(int argc, char **argv) { return run_tests(argc, argv); }
