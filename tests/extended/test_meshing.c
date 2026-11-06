/* Chunk meshing and drawing (src/world/chunkmesh.c, blockmesh.c, chunk.c).
 *
 * Observation seam: the fake OpenGL layer. A chunk is meshed with the public
 * chunk_prepare() and drawn with chunk_render(chunk, BASE / TRANSPARENT); the
 * number of indices passed to glDrawElements is what we measure.
 *
 * To stay independent of the vertex/index format, every expectation is
 * expressed in "faces": the indices drawn for one isolated opaque cube are
 * measured in-test and divided by its 6 faces to obtain the indices-per-face
 * unit F. All other scenarios are compared against multiples of F.
 *
 * Minimal global/world state needed by the production code (documented here
 * because it is test setup, not behaviour under test):
 *   - state.renderer.block_atlas: built with the production blockatlas_create()
 *     from res/images/blocks.png, since meshing looks up texture coordinates.
 *   - world->entity_view: an entity with a position component (and a camera
 *     component for the world_render test), because transparent faces are depth
 *     sorted relative to the viewer.
 *   - world->throttles.mesh.count is reset to 0 before each prepare so that the
 *     per-frame meshing budget never hides a rebuild (the game does this in
 *     world_update every frame).
 */
#include "check.h"
#include "harness.h"
#include "entity/c_position.h"
#include "entity/c_camera.h"
#include "gfx/window.h"

#define V(x, y, z) ((ivec3s){{ (x), (y), (z) }})
static const ivec3s ORIGIN = {{ 0, 0, 0 }};

static void setup_view(struct World *w, vec3s pos, bool with_camera) {
    struct Entity e = ecs_new(&w->ecs);
    ivec3s block = world_pos_to_block(pos);
    ecs_add(e, C_POSITION, ((struct PositionComponent) {
        .position = pos, .block = block, .offset = world_pos_to_offset(block) }));
    if (with_camera) {
        window.size = (ivec2s) {{ 1280, 720 }};
        ecs_add(e, C_CAMERA, ((struct CameraComponent) { 0 }));
    }
    w->entity_view = e;
    w->entity_load = e;
}

static void setup_atlas(void) {
    harness_init();
    state.renderer.block_atlas = blockatlas_create("res/images/blocks.png", (ivec2s) {{ 16, 16 }});
}

/* A world with a single chunk slot at offset (0,0,0), viewer far away. */
static struct World *one_chunk_world(const struct TWBlock *blocks, size_t n, struct Chunk **out) {
    setup_atlas();
    struct World *w = tw_create(V(16, 16, 16), 1, 1);
    setup_view(w, (vec3s) {{ 100.5f, 100.5f, 100.5f }}, false);
    *out = tw_load_prefilled(w, ORIGIN, blocks, n);
    return w;
}

struct Drawn { size_t base, transparent; };

static void mesh_chunk(struct World *w, struct Chunk *c) {
    w->throttles.mesh.count = 0;
    chunk_prepare(c);
}

static struct Drawn draw_chunk(struct Chunk *c) {
    struct Drawn d;
    fake_gl_reset_stats();
    chunk_render(c, BASE);
    d.base = fake_gl.drawn_indices;
    fake_gl_reset_stats();
    chunk_render(c, TRANSPARENT);
    d.transparent = fake_gl.drawn_indices;
    return d;
}

static struct Drawn mesh_and_draw(struct World *w, struct Chunk *c) {
    mesh_chunk(w, c);
    return draw_chunk(c);
}

static struct Drawn scene(const struct TWBlock *blocks, size_t n) {
    struct Chunk *c;
    struct World *w = one_chunk_world(blocks, n, &c);
    struct Drawn d = mesh_and_draw(w, c);
    tw_destroy(w);
    return d;
}

/* Indices drawn per visible face, measured from one isolated opaque cube. */
static size_t face_unit(void) {
    struct TWBlock b[] = { { V(10, 10, 10), STONE } };
    struct Drawn d = scene(b, 1);
    CHECK_MSG(d.base > 0, "an isolated stone block draws nothing");
    CHECK_MSG(d.base % 6 == 0, "isolated cube draws %zu indices, not a multiple of 6 faces", d.base);
    CHECK_EQ(d.transparent, 0);
    return d.base / 6;
}

#define N_(a) (sizeof(a) / sizeof((a)[0]))

TEST(empty_chunk_draws_nothing) {
    struct Chunk *c;
    struct World *w = one_chunk_world(NULL, 0, &c);
    struct Drawn d = mesh_and_draw(w, c);
    CHECK_EQ(d.base, 0);
    CHECK_EQ(d.transparent, 0);
    tw_destroy(w);
}

TEST(isolated_opaque_cube_draws_six_equal_faces_in_base_pass) {
    size_t f = face_unit();
    CHECK(f > 0);
    /* every opaque block type with a default cube mesh draws the same */
    enum BlockId opaque[] = { GRASS, DIRT, STONE, SAND, LOG, COAL, COPPER, CLAY, GRAVEL,
                              PLANKS, COBBLESTONE, SNOW, PODZOL, PINE_LOG };
    for (size_t i = 0; i < N_(opaque); i++) {
        struct TWBlock b[] = { { V(5, 20, 7), opaque[i] } };
        struct Drawn d = scene(b, 1);
        CHECK_MSG(d.base == 6 * f && d.transparent == 0,
                  "block %d: base=%zu transparent=%zu, expected %zu/0", opaque[i], d.base, d.transparent, 6 * f);
    }
}

TEST(two_adjacent_opaque_cubes_cull_shared_face) {
    size_t f = face_unit();
    ivec3s dirs[] = { V(1, 0, 0), V(0, 1, 0), V(0, 0, 1) };
    for (size_t i = 0; i < 3; i++) {
        ivec3s p = V(10, 10, 10);
        struct TWBlock b[] = { { p, STONE }, { glms_ivec3_add(p, dirs[i]), DIRT } };
        struct Drawn d = scene(b, 2);
        CHECK_EQ(d.base, 10 * f);
        CHECK_EQ(d.transparent, 0);
    }
}

TEST(diagonal_opaque_cubes_cull_nothing) {
    size_t f = face_unit();
    struct TWBlock b[] = { { V(10, 10, 10), STONE }, { V(11, 11, 10), STONE }, { V(12, 12, 12), STONE } };
    CHECK_EQ(scene(b, 3).base, 18 * f);
}

TEST(solid_2x2x2_draws_only_exterior_faces) {
    size_t f = face_unit();
    struct TWBlock b[8];
    size_t n = 0;
    for (int x = 0; x < 2; x++) for (int y = 0; y < 2; y++) for (int z = 0; z < 2; z++)
        b[n++] = (struct TWBlock) { V(4 + x, 4 + y, 4 + z), STONE };
    struct Drawn d = scene(b, n);
    CHECK_EQ(d.base, 24 * f);
    CHECK_EQ(d.transparent, 0);
}

TEST(enclosed_block_contributes_no_faces) {
    size_t f = face_unit();
    /* 3x3x3 solid cube: only the 6 * 9 exterior faces are visible */
    struct TWBlock b[27];
    size_t n = 0;
    for (int x = 0; x < 3; x++) for (int y = 0; y < 3; y++) for (int z = 0; z < 3; z++)
        b[n++] = (struct TWBlock) { V(8 + x, 8 + y, 8 + z), STONE };
    CHECK_EQ(scene(b, n).base, 54 * f);
}

TEST(vertex_upload_scales_with_visible_faces) {
    /* Format-independent: bytes of vertex data for 2 adjacent cubes (10 faces)
     * relative to one cube (6 faces). */
    struct Chunk *c;
    struct TWBlock one[] = { { V(10, 10, 10), STONE } };
    struct World *w = one_chunk_world(one, 1, &c);
    fake_gl_reset_stats();
    mesh_chunk(w, c);
    size_t bytes1 = fake_gl.array_bytes;
    tw_destroy(w);

    struct TWBlock two[] = { { V(10, 10, 10), STONE }, { V(10, 11, 10), STONE } };
    w = one_chunk_world(two, 2, &c);
    fake_gl_reset_stats();
    mesh_chunk(w, c);
    size_t bytes2 = fake_gl.array_bytes;
    tw_destroy(w);

    CHECK(bytes1 > 0);
    CHECK_EQ(bytes2 * 6, bytes1 * 10);
}

TEST(transparent_block_alone_goes_to_transparent_pass) {
    size_t f = face_unit();
    enum BlockId tr[] = { GLASS, WATER, LEAVES, PINE_LEAVES, LAVA };
    for (size_t i = 0; i < N_(tr); i++) {
        struct TWBlock b[] = { { V(10, 10, 10), tr[i] } };
        struct Drawn d = scene(b, 1);
        CHECK_MSG(d.base == 0 && d.transparent == 6 * f,
                  "block %d: base=%zu transparent=%zu, expected 0/%zu", tr[i], d.base, d.transparent, 6 * f);
    }
}

TEST(opaque_face_next_to_transparent_block_is_kept) {
    size_t f = face_unit();
    enum BlockId tr[] = { GLASS, WATER, LEAVES };
    for (size_t i = 0; i < N_(tr); i++) {
        struct TWBlock b[] = { { V(10, 10, 10), STONE }, { V(11, 10, 10), tr[i] } };
        struct Drawn d = scene(b, 2);
        /* stone keeps all 6 faces; the transparent block's face against the
         * opaque stone is hidden, its other 5 faces are drawn */
        CHECK_EQ(d.base, 6 * f);
        CHECK_EQ(d.transparent, 5 * f);
    }
}

TEST(same_transparent_blocks_cull_shared_face) {
    size_t f = face_unit();
    struct TWBlock g[] = { { V(10, 10, 10), GLASS }, { V(10, 10, 11), GLASS } };
    struct Drawn d = scene(g, 2);
    CHECK_EQ(d.base, 0);
    CHECK_EQ(d.transparent, 10 * f);

    struct TWBlock water[] = { { V(10, 10, 10), WATER }, { V(10, 9, 10), WATER } };
    d = scene(water, 2);
    CHECK_EQ(d.transparent, 10 * f);
}

TEST(different_transparent_blocks_keep_shared_faces) {
    size_t f = face_unit();
    struct TWBlock b[] = { { V(10, 10, 10), GLASS }, { V(10, 11, 10), WATER } };
    struct Drawn d = scene(b, 2);
    CHECK_EQ(d.base, 0);
    CHECK_EQ(d.transparent, 12 * f);
}

TEST(sprites_are_transparent_and_do_not_hide_neighbours) {
    size_t f = face_unit();
    enum BlockId sprites[] = { ROSE, BUTTERCUP, SHRUB, TALLGRASS };
    for (size_t i = 0; i < N_(sprites); i++) {
        struct TWBlock alone[] = { { V(10, 11, 10), sprites[i] } };
        struct Drawn ds = scene(alone, 1);
        CHECK_EQ(ds.base, 0);
        CHECK(ds.transparent > 0);

        /* a sprite standing on stone: stone still draws its top face, sprite unchanged */
        struct TWBlock on_stone[] = { { V(10, 10, 10), STONE }, { V(10, 11, 10), sprites[i] } };
        struct Drawn d = scene(on_stone, 2);
        CHECK_EQ(d.base, 6 * f);
        CHECK_EQ(d.transparent, ds.transparent);
    }
}

TEST(torch_is_transparent_and_drawn_on_all_sides_even_when_enclosed) {
    size_t f = face_unit();
    struct TWBlock alone[] = { { V(10, 10, 10), TORCH } };
    struct Drawn d = scene(alone, 1);
    CHECK_EQ(d.base, 0);
    CHECK_EQ(d.transparent, 6 * f);

    /* the torch is smaller than a block, so its faces are never culled by neighbours */
    struct TWBlock boxed[7] = { { V(10, 10, 10), TORCH } };
    for (int i = 0; i < 6; i++)
        boxed[1 + i] = (struct TWBlock) { glms_ivec3_add(V(10, 10, 10), DIR2IVEC3S(i)), STONE };
    d = scene(boxed, 7);
    CHECK_EQ(d.transparent, 6 * f);
    CHECK_EQ(d.base, 6 * 6 * f); /* torch is transparent: stones keep their faces towards it */
}

TEST(faces_are_culled_across_chunk_borders) {
    size_t f = face_unit();
    setup_atlas();
    struct World *w = tw_create(V(32, 16, 16), 2, 1); /* chunks x in {0,1} (and others) */
    setup_view(w, (vec3s) {{ 200.5f, 200.5f, 200.5f }}, false);
    struct TWBlock a[] = { { V(31, 10, 10), STONE } };
    struct TWBlock b[] = { { V(0, 10, 10), STONE } };
    struct Chunk *ca = tw_load_prefilled(w, V(0, 0, 0), a, 1);
    struct Chunk *cb = tw_load_prefilled(w, V(1, 0, 0), b, 1);
    CHECK(ca && cb && ca != cb);
    struct Drawn da = mesh_and_draw(w, ca), db = mesh_and_draw(w, cb);
    CHECK_EQ(da.base, 5 * f);
    CHECK_EQ(db.base, 5 * f);
    tw_destroy(w);
}

TEST(modifying_a_block_marks_chunk_dirty_and_remesh_reflects_change) {
    size_t f = face_unit();
    struct Chunk *c;
    struct TWBlock b[] = { { V(10, 10, 10), STONE } };
    struct World *w = one_chunk_world(b, 1, &c);
    CHECK_EQ(mesh_and_draw(w, c).base, 6 * f);
    CHECK(!tw_chunk_mesh_dirty(c));

    world_set_block(w, V(10, 10, 11), STONE);
    CHECK(tw_chunk_mesh_dirty(c));
    CHECK_EQ(mesh_and_draw(w, c).base, 10 * f);

    world_set_block(w, V(10, 10, 12), GLASS);
    struct Drawn d = mesh_and_draw(w, c);
    CHECK_EQ(d.base, 10 * f);
    CHECK_EQ(d.transparent, 5 * f);

    /* replacing a block with another opaque block keeps the geometry */
    world_set_block(w, V(10, 10, 11), DIRT);
    CHECK(tw_chunk_mesh_dirty(c));
    d = mesh_and_draw(w, c);
    CHECK_EQ(d.base, 10 * f);
    CHECK_EQ(d.transparent, 5 * f);
    tw_destroy(w);
}

TEST(removing_all_blocks_draws_nothing_again) {
    size_t f = face_unit();
    struct Chunk *c;
    struct TWBlock b[] = { { V(3, 3, 3), STONE }, { V(3, 4, 3), GLASS }, { V(20, 3, 3), ROSE } };
    struct World *w = one_chunk_world(b, 3, &c);
    struct Drawn d = mesh_and_draw(w, c);
    CHECK(d.base == 6 * f && d.transparent > 0);

    world_set_block(w, V(3, 3, 3), AIR);
    world_set_block(w, V(3, 4, 3), AIR);
    world_set_block(w, V(20, 3, 3), AIR);
    d = mesh_and_draw(w, c);
    CHECK_EQ(d.base, 0);
    CHECK_EQ(d.transparent, 0);
    tw_destroy(w);
}

TEST(unchanged_chunk_is_not_remeshed_and_draws_the_same) {
    struct Chunk *c;
    struct TWBlock b[] = { { V(3, 3, 3), STONE }, { V(3, 4, 3), WATER } };
    struct World *w = one_chunk_world(b, 2, &c);
    struct Drawn d1 = mesh_and_draw(w, c);
    fake_gl_reset_stats();
    mesh_chunk(w, c);
    CHECK_EQ(fake_gl.array_bytes, 0);
    CHECK_EQ(fake_gl.element_bytes, 0);
    struct Drawn d2 = draw_chunk(c);
    CHECK_EQ(d1.base, d2.base);
    CHECK_EQ(d1.transparent, d2.transparent);
    tw_destroy(w);
}

TEST(boundary_modification_marks_horizontal_and_lower_neighbours_dirty) {
    /* +Y is covered by an already-known defect elsewhere; here -X,+X,-Y,-Z,+Z */
    setup_atlas();
    struct World *w = tw_create(V(16, 16, 16), 3, 1); /* chunk offsets -1..1 */
    setup_view(w, (vec3s) {{ 16.5f, 16.5f, 16.5f }}, false);
    tw_fill_empty(w);
    struct { ivec3s local, neighbour; } cases[] = {
        { V(0, 10, 10), V(-1, 0, 0) },
        { V(31, 10, 10), V(1, 0, 0) },
        { V(10, 0, 10), V(0, -1, 0) },
        { V(10, 10, 0), V(0, 0, -1) },
        { V(10, 10, 31), V(0, 0, 1) },
    };
    for (size_t i = 0; i < N_(cases); i++) {
        struct Chunk *n = world_get_chunk(w, cases[i].neighbour);
        CHECK(n != NULL);
        tw_chunk_mesh_clear_dirty(n);
        world_set_block(w, tw_pos(ORIGIN, cases[i].local), STONE);
        CHECK_MSG(tw_chunk_mesh_dirty(n), "case %zu: neighbour (%d,%d,%d) not dirty", i,
                  cases[i].neighbour.x, cases[i].neighbour.y, cases[i].neighbour.z);
    }
    /* an interior modification does not dirty neighbours */
    struct Chunk *nx = world_get_chunk(w, V(1, 0, 0));
    tw_chunk_mesh_clear_dirty(nx);
    world_set_block(w, V(15, 15, 15), STONE);
    CHECK(!tw_chunk_mesh_dirty(nx));
    tw_destroy(w);
}

/* Visible-face oracle for the culling rule, evaluated with world_get_block. */
static void oracle_faces(struct World *w, struct Chunk *c, size_t *base_faces, size_t *tr_faces, size_t *sprites) {
    *base_faces = *tr_faces = *sprites = 0;
    for (int x = 0; x < CHUNK_SIZE_X; x++) for (int y = 0; y < CHUNK_SIZE_Y; y++) for (int z = 0; z < CHUNK_SIZE_Z; z++) {
        ivec3s p = glms_ivec3_add(c->position, V(x, y, z));
        struct Block *b = &BLOCKS[world_get_block(w, p)];
        if (b->id == AIR) continue;
        if (b->mesh_type == BLOCKMESH_SPRITE) { (*sprites)++; continue; }
        for (int d = 0; d < 6; d++) {
            struct Block *n = &BLOCKS[world_get_block(w, glms_ivec3_add(p, DIR2IVEC3S(d)))];
            bool visible = b->mesh_type == BLOCKMESH_CUSTOM ||
                (!b->transparent && n->transparent) ||
                (b->transparent && n->transparent && n->id != b->id);
            if (visible) { if (b->transparent) (*tr_faces)++; else (*base_faces)++; }
        }
    }
}

TEST(generated_terrain_mesh_matches_visible_face_oracle) {
    size_t f = face_unit();
    /* indices for one sprite, measured */
    struct TWBlock rose[] = { { V(10, 10, 10), ROSE } };
    size_t sprite = scene(rose, 1).transparent;

    setup_atlas();
    struct World *w = tw_create(V(0, 0, 0), 3, 42);
    setup_view(w, (vec3s) {{ 0.5f, 60.5f, 0.5f }}, false);
    tw_fill_generated(w);
    size_t checked = 0, nonempty = 0;
    /* Positions outside the loaded area read as AIR for both the mesher and the oracle. */
    for (int cx = -1; cx <= 1; cx++) for (int cy = -1; cy <= 1; cy++) for (int cz = -1; cz <= 1; cz++) {
        struct Chunk *c = world_get_chunk(w, V(cx, cy, cz));
        CHECK(c != NULL);
        size_t bf, tf, sp;
        oracle_faces(w, c, &bf, &tf, &sp);
        struct Drawn d = mesh_and_draw(w, c);
        CHECK_MSG(d.base == bf * f, "chunk (%d,%d,%d) base %zu != oracle %zu faces * %zu", cx, cy, cz, d.base, bf, f);
        CHECK_MSG(d.transparent == tf * f + sp * sprite,
                  "chunk (%d,%d,%d) transparent %zu != oracle %zu faces * %zu + %zu sprites * %zu",
                  cx, cy, cz, d.transparent, tf, f, sp, sprite);
        checked++;
        nonempty += bf > 0;
    }
    CHECK(checked == 27);
    CHECK_MSG(nonempty > 0, "generated terrain produced no visible faces (seed changed?)");
    tw_destroy(w);
}

TEST(world_render_draws_chunk_geometry_on_top_of_sky) {
    size_t f = face_unit();
    setup_atlas();
    /* empty world: whatever is drawn is sky only */
    struct World *w = tw_create(V(16, 16, 16), 1, 1);
    setup_view(w, (vec3s) {{ 16.5f, 40.5f, 16.5f }}, true);
    w->ticks = DAY_TICKS / 2;
    tw_load_empty(w, ORIGIN);
    w->throttles.mesh.count = 0;
    fake_gl_reset_stats();
    world_render(w);
    size_t sky_only = fake_gl.drawn_indices;
    tw_destroy(w);

    struct TWBlock b[] = { { V(10, 10, 10), STONE }, { V(12, 10, 10), GLASS } };
    w = tw_create(V(16, 16, 16), 1, 1);
    setup_view(w, (vec3s) {{ 16.5f, 40.5f, 16.5f }}, true);
    w->ticks = DAY_TICKS / 2;
    tw_load_prefilled(w, ORIGIN, b, 2);
    w->throttles.mesh.count = 0;
    fake_gl_reset_stats();
    world_render(w);
    CHECK_EQ(fake_gl.drawn_indices, sky_only + 12 * f);
    tw_destroy(w);
}

int main(int argc, char **argv) { return run_tests(argc, argv); }
