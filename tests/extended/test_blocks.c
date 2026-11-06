/* Block registry (src/block/, registered by block_init via harness_init).
 *
 * Checks the properties every block definition must satisfy for meshing,
 * lighting and physics to work, plus a few content facts that the game
 * depends on (air is empty, torches and lava glow, water and lava are liquids).
 * The block atlas size is taken from the production blockatlas_create() on
 * res/images/blocks.png rather than hard-coded.
 */
#include "check.h"
#include "harness.h"

#define V(x, y, z) ((ivec3s){{ (x), (y), (z) }})
#define FOR_BLOCKS(_id) for (int _id = 0; _id <= BLOCK_ID_LAST; _id++)

static ivec3s POSITIONS[] = { {{ 0, 0, 0 }}, {{ 5, 70, -3 }}, {{ -40, -2, 33 }}, {{ 31, 31, 31 }} };
#define NPOS (sizeof POSITIONS / sizeof POSITIONS[0])

static struct BlockAtlas load_atlas(void) {
    harness_init();
    return blockatlas_create("res/images/blocks.png", (ivec2s) {{ 16, 16 }});
}

static bool in_atlas(struct BlockAtlas *a, ivec2s p) {
    return p.x >= 0 && p.y >= 0 && p.x < a->size_sprites.x && p.y < a->size_sprites.y;
}

TEST(every_block_id_is_registered_with_its_own_id) {
    harness_init();
    FOR_BLOCKS(id) {
        struct Block *b = &BLOCKS[id];
        CHECK_MSG(b->id == (enum BlockId) id, "BLOCKS[%d].id == %d", id, b->id);
        CHECK(b->get_texture_location && b->get_mesh_information && b->get_animation_frames &&
              b->get_torchlight && b->get_aabb);
    }
}

TEST(air_is_empty_space) {
    harness_init();
    struct Block *air = &BLOCKS[AIR];
    CHECK(air->transparent);
    CHECK(!air->solid);
    CHECK(!air->liquid);
    CHECK(!air->can_emit_light);
    struct World *w = tw_create(V(0, 0, 0), 1, 1);
    for (size_t i = 0; i < NPOS; i++) CHECK_EQ(air->get_torchlight(w, POSITIONS[i]), 0);
    tw_destroy(w);
}

TEST(torch_and_lava_emit_light_and_other_blocks_do_not) {
    harness_init();
    struct World *w = tw_create(V(0, 0, 0), 1, 1);
    CHECK(BLOCKS[TORCH].can_emit_light);
    CHECK(BLOCKS[LAVA].can_emit_light);
    FOR_BLOCKS(id) {
        for (size_t i = 0; i < NPOS; i++) {
            Torchlight l = BLOCKS[id].get_torchlight(w, POSITIONS[i]);
            if (BLOCKS[id].can_emit_light) {
                /* non-zero intensity and at least one colour channel */
                CHECK_MSG((l & 0xF) != 0 && (l & 0xFFF0) != 0, "emitter %d has torchlight %#x", id, l);
            } else {
                CHECK_MSG(l == 0, "non-emitter %d reports torchlight %#x", id, l);
            }
        }
    }
    tw_destroy(w);
}

TEST(liquids_are_transparent_and_not_solid) {
    harness_init();
    CHECK(BLOCKS[WATER].liquid);
    CHECK(BLOCKS[LAVA].liquid);
    FOR_BLOCKS(id) {
        struct Block *b = &BLOCKS[id];
        if (b->liquid) {
            CHECK_MSG(!b->solid && b->transparent, "liquid %d must be non-solid and transparent", id);
            CHECK(b->mesh_type == BLOCKMESH_LIQUID);
        }
        if (b->mesh_type == BLOCKMESH_LIQUID) CHECK(b->liquid);
    }
}

TEST(non_cube_meshes_are_transparent) {
    /* a block that does not fill its cell must not hide its neighbours' faces */
    harness_init();
    FOR_BLOCKS(id) {
        struct Block *b = &BLOCKS[id];
        if (b->mesh_type != BLOCKMESH_DEFAULT)
            CHECK_MSG(b->transparent, "block %d with mesh type %d is not transparent", id, b->mesh_type);
        if (b->mesh_type == BLOCKMESH_SPRITE)
            CHECK_MSG(!b->solid, "sprite block %d is solid", id);
    }
}

TEST(physics_modifiers_are_positive) {
    harness_init();
    FOR_BLOCKS(id) {
        struct Block *b = &BLOCKS[id];
        CHECK(b->gravity_modifier > 0 && b->drag > 0 && b->sliperiness > 0);
    }
    /* water slows things down relative to air */
    CHECK(BLOCKS[WATER].drag > BLOCKS[AIR].drag);
    CHECK(BLOCKS[WATER].gravity_modifier < BLOCKS[AIR].gravity_modifier);
}

TEST(solid_default_blocks_have_unit_cube_aabb_at_position) {
    harness_init();
    struct World *w = tw_create(V(0, 0, 0), 1, 1);
    size_t solid = 0;
    FOR_BLOCKS(id) {
        struct Block *b = &BLOCKS[id];
        if (!b->solid || b->mesh_type != BLOCKMESH_DEFAULT) continue;
        solid++;
        for (size_t i = 0; i < NPOS; i++) {
            ivec3s p = POSITIONS[i];
            AABB box;
            b->get_aabb(w, p, box);
            for (int k = 0; k < 3; k++) {
                CHECK_NEAR(box[0].raw[k], p.raw[k], 1e-6);
                CHECK_NEAR(box[1].raw[k], p.raw[k] + 1.0, 1e-6);
            }
        }
    }
    CHECK(solid >= 10);
    CHECK(BLOCKS[STONE].solid && BLOCKS[DIRT].solid && BLOCKS[GRASS].solid);
    tw_destroy(w);
}

TEST(texture_locations_lie_inside_block_atlas) {
    struct BlockAtlas atlas = load_atlas();
    CHECK(atlas.size_sprites.x > 0 && atlas.size_sprites.y > 0);
    struct World *w = tw_create(V(0, 0, 0), 1, 1);
    /* AIR is never meshed and has no texture */
    for (int id = 1; id <= BLOCK_ID_LAST; id++) {
        for (size_t i = 0; i < NPOS; i++) for (int d = 0; d < 6; d++) {
            ivec2s t = BLOCKS[id].get_texture_location(w, POSITIONS[i], d);
            CHECK_MSG(in_atlas(&atlas, t), "block %d dir %d -> (%d,%d) outside %dx%d atlas",
                      id, d, t.x, t.y, atlas.size_sprites.x, atlas.size_sprites.y);
        }
    }
    tw_destroy(w);
}

TEST(animation_frames_lie_inside_block_atlas) {
    struct BlockAtlas atlas = load_atlas();
    size_t animated = 0;
    FOR_BLOCKS(id) {
        if (!BLOCKS[id].animated) continue;
        animated++;
        ivec2s frames[BLOCK_ATLAS_FRAMES];
        BLOCKS[id].get_animation_frames(frames);
        for (int f = 0; f < BLOCK_ATLAS_FRAMES; f++)
            CHECK_MSG(in_atlas(&atlas, frames[f]), "block %d frame %d outside atlas", id, f);
    }
    CHECK(animated >= 2); /* water and lava */
}

KNOWN_DEFECT(animated_blocks_are_textured_with_their_first_animation_frame,
             "lava's animation frames are (14, i) (a column of unused placeholder sprites) "
             "while its texture location is (0, 14); x/y are swapped (water uses (i, 15) and "
             "(0, 15)), so blockatlas_update animates a sprite lava never samples and lava is static") {
    harness_init();
    struct World *w = tw_create(V(0, 0, 0), 1, 1);
    FOR_BLOCKS(id) {
        if (!BLOCKS[id].animated) continue;
        ivec2s frames[BLOCK_ATLAS_FRAMES];
        BLOCKS[id].get_animation_frames(frames);
        for (int d = 0; d < 6; d++) {
            ivec2s t = BLOCKS[id].get_texture_location(w, POSITIONS[1], d);
            CHECK_MSG(t.x == frames[0].x && t.y == frames[0].y,
                      "block %d: texture (%d,%d) is not animation frame 0 (%d,%d)",
                      id, t.x, t.y, frames[0].x, frames[0].y);
        }
    }
    tw_destroy(w);
}

TEST(custom_mesh_blocks_fit_inside_their_cell_and_sprite) {
    struct BlockAtlas atlas = load_atlas();
    struct World *w = tw_create(V(0, 0, 0), 1, 1);
    size_t custom = 0;
    FOR_BLOCKS(id) {
        struct Block *b = &BLOCKS[id];
        if (b->mesh_type != BLOCKMESH_CUSTOM) continue;
        custom++;
        for (int d = 0; d < 6; d++) {
            vec3s off, size;
            ivec2s uv_off, uv_size;
            b->get_mesh_information(w, POSITIONS[1], d, &off, &size, &uv_off, &uv_size);
            for (int k = 0; k < 3; k++) {
                CHECK_MSG(size.raw[k] > 0 && size.raw[k] <= 1, "block %d dir %d size[%d]=%f", id, d, k, size.raw[k]);
                CHECK(off.raw[k] >= 0 && off.raw[k] + size.raw[k] <= 1 + 1e-6);
            }
            for (int k = 0; k < 2; k++) {
                CHECK(uv_size.raw[k] > 0 && uv_off.raw[k] >= 0);
                CHECK(uv_off.raw[k] + uv_size.raw[k] <= atlas.sprite_size.raw[k]);
            }
        }
    }
    CHECK(custom >= 1); /* the torch */
    tw_destroy(w);
}

int main(int argc, char **argv) { return run_tests(argc, argv); }
