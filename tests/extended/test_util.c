/* Pure utility functions: src/util/fmath.[ch], direction.c, aabb.h, util.h.
 *
 * The ray_block() checks here complement tests/math/test_math.c (which covers
 * axis-aligned casts and the distance limit): random rays with a fixed seed are
 * compared against an independent analytic ray/box (slab) oracle, and the
 * traversal itself is checked to be a face-connected walk along the ray.
 * Ray origins are kept away from grid planes, and direction components away
 * from zero, where the voxel walk is ill-conditioned by construction.
 */
#include "check.h"
#include "harness.h"

#define V(x, y, z) ((ivec3s){{ (x), (y), (z) }})

/* ---------------- ivec3s helpers ---------------- */

TEST(ivec3scmp_is_zero_exactly_for_equal_vectors) {
    CHECK(ivec3scmp(V(1, 2, 3), V(1, 2, 3)) == 0);
    CHECK(ivec3scmp(V(-5, 0, 7), V(-5, 0, 7)) == 0);
    CHECK(ivec3scmp(V(0, 0, 0), V(0, 0, 0)) == 0);
    test_rng_seed(5);
    for (int i = 0; i < 2000; i++) {
        ivec3s a = V((int) test_rng_range(-3, 3), (int) test_rng_range(-3, 3), (int) test_rng_range(-3, 3));
        ivec3s b = V((int) test_rng_range(-3, 3), (int) test_rng_range(-3, 3), (int) test_rng_range(-3, 3));
        bool eq = a.x == b.x && a.y == b.y && a.z == b.z;
        CHECK((ivec3scmp(a, b) == 0) == eq);
        CHECK((ivec3scmp(a, b) == 0) == (ivec3scmp(b, a) == 0));
    }
    /* each component matters, including sign and large magnitudes */
    CHECK(ivec3scmp(V(1, 2, 3), V(1, 2, -3)) != 0);
    CHECK(ivec3scmp(V(INT32_MAX, 0, 0), V(INT32_MIN, 0, 0)) != 0);
    CHECK(ivec3scmp(V(0, 1, 0), V(0, 0, 1)) != 0);
}

static int cmp_s64(const void *a, const void *b) {
    s64 x = *(const s64 *) a, y = *(const s64 *) b;
    return (x > y) - (x < y);
}

TEST(ivec3shash_is_deterministic_and_spreads_a_small_grid) {
    enum { R = 8, N = (2 * R + 1) * (2 * R + 1) * (2 * R + 1) };
    static s64 hashes[N];
    size_t n = 0;
    for (int x = -R; x <= R; x++) for (int y = -R; y <= R; y++) for (int z = -R; z <= R; z++) {
        s64 h = ivec3shash(V(x, y, z));
        CHECK(h == ivec3shash(V(x, y, z)));
        hashes[n++] = h;
    }
    qsort(hashes, n, sizeof hashes[0], cmp_s64);
    size_t collisions = 0;
    for (size_t i = 1; i < n; i++) collisions += hashes[i] == hashes[i - 1];
    CHECK_MSG(collisions == 0, "%zu hash collisions among %zu grid vectors", collisions, n);
    /* permutations of the same components are distinguished */
    CHECK(ivec3shash(V(1, 2, 3)) != ivec3shash(V(3, 2, 1)));
    CHECK(ivec3shash(V(0, 0, 1)) != ivec3shash(V(1, 0, 0)));
}

/* ---------------- scalar macros ---------------- */

TEST(sign_clamp_min_max_lerp_macros) {
    CHECK_EQ(sign(-7), -1);
    CHECK_EQ(sign(0), 0);
    CHECK_EQ(sign(42), 1);
    CHECK(sign(-0.25f) == -1.0f && sign(0.0f) == 0.0f && sign(3.5f) == 1.0f);
    CHECK(sign(-1e-30) == -1.0);

    CHECK_EQ(clamp(5, 0, 3), 3);
    CHECK_EQ(clamp(-5, 0, 3), 0);
    CHECK_EQ(clamp(2, 0, 3), 2);
    CHECK(clamp(0.5f, -1.0f, 1.0f) == 0.5f);
    CHECK(clamp(2.0f, -1.0f, 1.0f) == 1.0f);
    CHECK_EQ(min(3, -4), -4);
    CHECK_EQ(max(3, -4), 3);

    /* macro arguments are evaluated once */
    int i = 0;
    CHECK_EQ(max(i++, -1), 0);
    CHECK_EQ(i, 1);
    CHECK_EQ(clamp(i++, 0, 10), 1);
    CHECK_EQ(i, 2);

    CHECK_NEAR(lerpf(2.0f, 6.0f, 0.0f), 2.0, 1e-6);
    CHECK_NEAR(lerpf(2.0f, 6.0f, 1.0f), 6.0, 1e-6);
    CHECK_NEAR(lerpf(2.0f, 6.0f, 0.25f), 3.0, 1e-6);
    CHECK_NEAR(lerpf(-1.0f, 1.0f, 0.5f), 0.0, 1e-6);

    CHECK_NEAR(radians(180.0f), PI, 1e-5);
    CHECK_NEAR(degrees(radians(37.0f)), 37.0, 1e-4);
    CHECK_EQ(floori(-0.5), -1);
    CHECK_EQ(floori(-1.0), -1);
    CHECK_EQ(floori(2.999), 2);

    for (float x = -3.0f; x <= 3.0f; x += 0.25f) {
        CHECK_NEAR(safe_expf(-x, 2.0f), -safe_expf(x, 2.0f), 1e-5); /* odd */
        CHECK_NEAR(fabsf(safe_expf(x, 3.0f)), powf(fabsf(x), 3.0f), 1e-4);
    }
}

/* ---------------- directions ---------------- */

TEST(directions_are_unit_axis_vectors_with_unique_opposites) {
    int sum[3] = { 0, 0, 0 };
    for (int d = 0; d < 6; d++) {
        ivec3s v = DIR2IVEC3S(d);
        CHECK_EQ(abs(v.x) + abs(v.y) + abs(v.z), 1);
        vec3s f = DIR2VEC3S(d);
        CHECK_NEAR(glms_vec3_norm(f), 1.0, 1e-6);
        CHECK(f.x == v.x && f.y == v.y && f.z == v.z);
        int opposites = 0;
        for (int e = 0; e < 6; e++) {
            ivec3s w = DIR2IVEC3S(e);
            if (v.x + w.x == 0 && v.y + w.y == 0 && v.z + w.z == 0) opposites++;
        }
        CHECK_EQ(opposites, 1);
        sum[0] += v.x; sum[1] += v.y; sum[2] += v.z;
        for (int e = 0; e < d; e++) CHECK(ivec3scmp(DIR2IVEC3S(e), v) != 0);
    }
    CHECK(sum[0] == 0 && sum[1] == 0 && sum[2] == 0);
    /* named directions */
    CHECK_IVEC3_EQ(DIR2IVEC3S(UP), V(0, 1, 0));
    CHECK_IVEC3_EQ(DIR2IVEC3S(DOWN), V(0, -1, 0));
    CHECK_IVEC3_EQ(DIR2IVEC3S(EAST), V(1, 0, 0));
    CHECK_IVEC3_EQ(DIR2IVEC3S(WEST), V(-1, 0, 0));
    CHECK_IVEC3_EQ(DIR2IVEC3S(NORTH), V(0, 0, -1));
    CHECK_IVEC3_EQ(DIR2IVEC3S(SOUTH), V(0, 0, 1));
}

/* ---------------- memsetl ---------------- */

TEST(memsetl_fills_exactly_the_requested_words) {
    u64 buf[12];
    for (int i = 0; i < 12; i++) buf[i] = 0xAAAAAAAAAAAAAAAAull;
    memsetl(buf + 2, 0x0123456789ABCDEFull, 8 * sizeof(u64));
    for (int i = 0; i < 12; i++) {
        u64 expect = (i >= 2 && i < 10) ? 0x0123456789ABCDEFull : 0xAAAAAAAAAAAAAAAAull;
        CHECK_MSG(buf[i] == expect, "word %d = %llx", i, (unsigned long long) buf[i]);
    }
    s64 neg[4] = { 0, 0, 0, 0 };
    memsetl(neg, (u64) LONG_MIN, sizeof neg);
    for (int i = 0; i < 4; i++) CHECK(neg[i] == LONG_MIN);
    memsetl(neg, 7, 0); /* zero size writes nothing */
    CHECK(neg[0] == LONG_MIN);
}

/* ---------------- AABB helpers ---------------- */

TEST(aabb_scale_keeps_centre_and_scales_extent) {
    test_rng_seed(11);
    for (int i = 0; i < 200; i++) {
        vec3s lo = {{ (float) test_rng_range(-50, 50) / 4, (float) test_rng_range(-50, 50) / 4, (float) test_rng_range(-50, 50) / 4 }};
        vec3s size = {{ (float) test_rng_range(1, 40) / 8, (float) test_rng_range(1, 40) / 8, (float) test_rng_range(1, 40) / 8 }};
        vec3s scale = {{ (float) test_rng_range(1, 30) / 10, (float) test_rng_range(1, 30) / 10, (float) test_rng_range(1, 30) / 10 }};
        AABB box = { lo, glms_vec3_add(lo, size) }, out;
        glms_aabb_scale(box, scale, out);
        for (int k = 0; k < 3; k++) {
            CHECK_NEAR((out[0].raw[k] + out[1].raw[k]) / 2, (box[0].raw[k] + box[1].raw[k]) / 2, 1e-4);
            CHECK_NEAR(out[1].raw[k] - out[0].raw[k], size.raw[k] * scale.raw[k], 1e-4);
        }
    }
}

TEST(aabb_depth_is_overlap_extent_and_symmetric) {
    /* unit box at origin vs box shifted by (0.25, 0.5, 0.9) */
    AABB a = { {{ 0, 0, 0 }}, {{ 1, 1, 1 }} };
    AABB b = { {{ 0.25f, 0.5f, 0.9f }}, {{ 1.25f, 1.5f, 1.9f }} };
    vec3s d = glms_aabb_aabb_depth(a, b), e = glms_aabb_aabb_depth(b, a);
    CHECK_NEAR(d.x, 0.75, 1e-6); CHECK_NEAR(d.y, 0.5, 1e-6); CHECK_NEAR(d.z, 0.1, 1e-6);
    for (int k = 0; k < 3; k++) CHECK_NEAR(d.raw[k], e.raw[k], 1e-6);
    /* separated along x: negative depth on that axis */
    AABB c = { {{ 2, 0, 0 }}, {{ 3, 1, 1 }} };
    CHECK(glms_aabb_aabb_depth(a, c).x < 0);
}

/* ---------------- ray_block against an analytic oracle ---------------- */

#define G 14               /* occupied cells live in [-G/2, G/2)^3 */
static bool grid[G][G][G];

struct Walk { ivec3s cells[512]; int n; };
static struct Walk walk;

static bool occupied_at(ivec3s p) {
    int x = p.x + G / 2, y = p.y + G / 2, z = p.z + G / 2;
    if (x < 0 || y < 0 || z < 0 || x >= G || y >= G || z >= G) return false;
    return grid[x][y][z];
}

static bool probe(void *arg, ivec3s p) {
    (void) arg;
    if (walk.n < 512) walk.cells[walk.n] = p;
    walk.n++;
    return occupied_at(p);
}

/* Ray (unit direction u) vs cell box: entry/exit distances; entry axis. */
static void slab(vec3s o, vec3s u, ivec3s c, double *entry, double *exit_, int *axis, double *gap) {
    double tmin[3], tmax[3];
    for (int k = 0; k < 3; k++) {
        double t1 = (c.raw[k] - o.raw[k]) / u.raw[k], t2 = (c.raw[k] + 1 - o.raw[k]) / u.raw[k];
        tmin[k] = fmin(t1, t2); tmax[k] = fmax(t1, t2);
    }
    int a = 0;
    for (int k = 1; k < 3; k++) if (tmin[k] > tmin[a]) a = k;
    double second = -INFINITY;
    for (int k = 0; k < 3; k++) if (k != a) second = fmax(second, tmin[k]);
    *entry = tmin[a];
    *exit_ = fmin(tmax[0], fmin(tmax[1], tmax[2]));
    *axis = a;
    *gap = tmin[a] - second;
}

static float away_from_grid(float v) {
    float fr = v - floorf(v);
    if (fr < 0.02f) v += 0.03f;
    if (fr > 0.98f) v -= 0.03f;
    return v;
}

TEST(ray_block_agrees_with_analytic_oracle_on_random_rays) {
    test_rng_seed(0xC0FFEE);
    for (int x = 0; x < G; x++) for (int y = 0; y < G; y++) for (int z = 0; z < G; z++)
        grid[x][y][z] = test_rng_range(0, 99) < 4;

    const double EPS = 1e-3;
    int hits = 0, misses = 0, faces_checked = 0;
    for (int r = 0; r < 3000; r++) {
        vec3s o;
        for (int k = 0; k < 3; k++)
            o.raw[k] = away_from_grid((float) test_rng_range(-5000, 5000) / 1000.0f);
        vec3s dir;
        for (int k = 0; k < 3; k++) {
            float c = (float) test_rng_range(50, 1000) / 1000.0f;
            dir.raw[k] = test_rng_range(0, 1) ? c : -c;
        }
        vec3s u = glms_vec3_normalize(dir);
        /* direction length must not matter: scale it arbitrarily */
        vec3s d_in = glms_vec3_scale(u, (float) test_rng_range(1, 40) / 10.0f);
        float max_dist = (float) test_rng_range(500, 12000) / 1000.0f;

        walk.n = 0;
        ivec3s out = V(9999, 9999, 9999);
        enum Direction face = (enum Direction) 99;
        bool hit = ray_block((struct Ray) { o, d_in }, max_dist, NULL, probe, &out, &face);

        /* the walk starts in the origin cell and moves to a face neighbour each step,
         * visiting only cells the ray actually passes through */
        CHECK(walk.n >= 1 && walk.n <= 512);
        CHECK_IVEC3_EQ(walk.cells[0], world_pos_to_block(o));
        for (int i = 0; i < walk.n; i++) {
            if (i > 0) {
                ivec3s a = walk.cells[i - 1], b = walk.cells[i];
                CHECK_MSG(abs(a.x - b.x) + abs(a.y - b.y) + abs(a.z - b.z) == 1, "ray %d: walk jumps at step %d", r, i);
            }
            double en, ex, gap; int ax;
            slab(o, u, walk.cells[i], &en, &ex, &ax, &gap);
            CHECK_MSG(en <= ex + EPS && ex >= -EPS, "ray %d: visited cell not on the ray", r);
            CHECK_MSG(fmax(en, 0) <= max_dist + EPS, "ray %d: visited a cell beyond max distance", r);
        }

        /* oracle: first occupied cell entered with a non-grazing intersection */
        double best = INFINITY;
        for (int x = 0; x < G; x++) for (int y = 0; y < G; y++) for (int z = 0; z < G; z++) {
            if (!grid[x][y][z]) continue;
            double en, ex, gap; int ax;
            slab(o, u, V(x - G / 2, y - G / 2, z - G / 2), &en, &ex, &ax, &gap);
            if (ex - fmax(en, 0) > EPS && ex > 0) best = fmin(best, fmax(en, 0));
        }

        if (hit) {
            hits++;
            CHECK_IVEC3_EQ(out, walk.cells[walk.n - 1]);
            CHECK(occupied_at(out));
            double en, ex, gap; int ax;
            slab(o, u, out, &en, &ex, &ax, &gap);
            CHECK_MSG(en <= ex + EPS, "ray %d: hit cell not on the ray", r);
            double e = fmax(en, 0);
            CHECK_MSG(e <= max_dist + EPS, "ray %d: hit at %f beyond max %f", r, e, max_dist);
            CHECK_MSG(e <= best + EPS, "ray %d: hit at %f but an occupied cell is entered at %f", r, e, best);
            if (en > EPS && gap > EPS) {
                /* the reported face is the one the ray entered through: it faces the ray */
                CHECK((int) face >= 0 && (int) face < 6);
                ivec3s n = DIR2IVEC3S(face);
                CHECK_MSG(n.raw[ax] != 0, "ray %d: face %d not on entry axis %d", r, face, ax);
                CHECK_MSG(n.raw[ax] * u.raw[ax] < 0, "ray %d: face %d does not face the ray", r, face);
                faces_checked++;
            }
        } else {
            misses++;
            CHECK_MSG(!(best < max_dist - EPS), "ray %d: missed an occupied cell entered at %f (max %f)", r, best, max_dist);
        }
    }
    CHECK_MSG(hits > 200 && misses > 200 && faces_checked > 100, "weak coverage: %d hits, %d misses, %d faces",
              hits, misses, faces_checked);
}

TEST(ray_block_never_hits_in_empty_space) {
    test_rng_seed(3);
    for (int r = 0; r < 200; r++) {
        vec3s o = {{ away_from_grid((float) test_rng_range(-9000, 9000) / 1000.0f),
                     away_from_grid((float) test_rng_range(-9000, 9000) / 1000.0f),
                     away_from_grid((float) test_rng_range(-9000, 9000) / 1000.0f) }};
        vec3s d = {{ 0.3f, -0.7f, 0.2f }};
        walk.n = 0;
        ivec3s out; enum Direction face;
        memset(grid, 0, sizeof grid);
        CHECK(!ray_block((struct Ray) { o, d }, 10.0f, NULL, probe, &out, &face));
        CHECK(walk.n > 1);
    }
}

int main(int argc, char **argv) { return run_tests(argc, argv); }
