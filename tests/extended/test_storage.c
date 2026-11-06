/* Packed per-block storage and world coordinate mapping, through the public
 * world API (world_get_* / world_set_*) and the real chunk code.
 *
 * The torchlight-clears-sunlight/metadata defect and the narrow metadata field
 * are asserted in test_known_defects.c; tests here avoid relying on either. */
#include "check.h"
#include "harness.h"

#define V(x, y, z) ((ivec3s){{ (x), (y), (z) }})

static struct World *open_air_world(ivec3s center, size_t size) {
    struct World *w = tw_create(center, size, 1);
    tw_fill_empty(w);
    return w;
}

TEST(fresh_air_world_has_air_full_sunlight_no_torchlight) {
    struct World *w = open_air_world(V(0, 0, 0), 2);
    test_rng_seed(1);
    for (int i = 0; i < 200; i++) {
        ivec3s p = V(test_rng_range(-32, 31), test_rng_range(-32, 31), test_rng_range(-32, 31));
        CHECK_EQ(world_get_block(w, p), AIR);
        CHECK_EQ(world_get_torchlight(w, p), 0);
        CHECK_EQ(world_get_sunlight(w, p), LIGHT_MAX);
    }
    tw_destroy(w);
}

TEST(sunlight_roundtrip_and_isolation) {
    struct World *w = open_air_world(V(16, 16, 16), 1);
    ivec3s p = V(7, 8, 9), q = V(7, 8, 10);
    world_set_metadata(w, p, 3);
    for (u32 s = 0; s <= LIGHT_MAX; s++) {
        world_set_sunlight(w, p, (Sunlight)s);
        CHECK_EQ(world_get_sunlight(w, p), s);
        CHECK_EQ(world_get_block(w, p), AIR);
        CHECK_EQ(world_get_torchlight(w, p), 0);
        CHECK_EQ(world_get_metadata(w, p), 3);
    }
    CHECK_EQ(world_get_sunlight(w, q), LIGHT_MAX); /* neighbour untouched */
    tw_destroy(w);
}

TEST(torchlight_roundtrip_all_channels) {
    struct World *w = open_air_world(V(16, 16, 16), 1);
    ivec3s p = V(4, 4, 4);
    for (u32 c = 0; c <= 15; c++) {
        Torchlight t = TORCHLIGHT_OF(c, 15 - c, c / 2, (c * 7) % 16);
        world_set_torchlight(w, p, t);
        CHECK_EQ(world_get_torchlight(w, p), t);
        CHECK_EQ(world_get_block(w, p), AIR);
    }
    tw_destroy(w);
}

TEST(sunlight_write_after_torchlight_keeps_torchlight) {
    struct World *w = open_air_world(V(16, 16, 16), 1);
    ivec3s p = V(4, 5, 6);
    world_set_torchlight(w, p, TORCHLIGHT_OF(15, 11, 5, 15));
    world_set_sunlight(w, p, 6);
    CHECK_EQ(world_get_torchlight(w, p), TORCHLIGHT_OF(15, 11, 5, 15));
    CHECK_EQ(world_get_sunlight(w, p), 6);
    tw_destroy(w);
}

TEST(combined_light_is_sunlight_and_torchlight) {
    struct World *w = open_air_world(V(16, 16, 16), 1);
    ivec3s p = V(20, 21, 22);
    world_set_torchlight(w, p, TORCHLIGHT_OF(1, 2, 3, 4));
    world_set_sunlight(w, p, 12);
    CHECK_EQ(world_get_light(w, p), LIGHT_OF(12, TORCHLIGHT_OF(1, 2, 3, 4)));
    world_set_light(w, p, LIGHT_OF(3, TORCHLIGHT_OF(9, 8, 7, 6)));
    CHECK_EQ(world_get_sunlight(w, p), 3);
    CHECK_EQ(world_get_torchlight(w, p), TORCHLIGHT_OF(9, 8, 7, 6));
    tw_destroy(w);
}

TEST(metadata_roundtrip_small_values_and_isolation) {
    struct World *w = open_air_world(V(16, 16, 16), 1);
    ivec3s p = V(1, 2, 3);
    world_set_sunlight(w, p, 7);
    for (u32 m = 0; m < 8; m++) { /* larger values: see test_known_defects.c */
        world_set_metadata(w, p, m);
        CHECK_EQ(world_get_metadata(w, p), m);
        CHECK_EQ(world_get_sunlight(w, p), 7);
        CHECK_EQ(world_get_block(w, p), AIR);
    }
    tw_destroy(w);
}

TEST(block_write_keeps_metadata_and_reads_back_every_block_id) {
    struct World *w = open_air_world(V(16, 16, 16), 1);
    ivec3s p = V(12, 13, 14);
    world_set_metadata(w, p, 6);
    for (int id = 0; id <= BLOCK_ID_LAST; id++) {
        world_set_block(w, p, (enum BlockId)id);
        CHECK_EQ(world_get_block(w, p), id);
        CHECK_EQ(world_get_metadata(w, p), 6);
    }
    tw_destroy(w);
}

TEST(each_cell_is_independent) {
    struct World *w = open_air_world(V(0, 0, 0), 2);
    /* write a distinct block into a sparse set of cells across 8 chunks,
       then verify all of them and that a probe of other cells is still air */
    test_rng_seed(7);
    ivec3s cells[300];
    enum BlockId ids[300];
    for (int i = 0; i < 300; i++) {
        cells[i] = V(test_rng_range(-32, 31), test_rng_range(-32, 31), test_rng_range(-32, 31));
        ids[i] = (enum BlockId)test_rng_range(1, BLOCK_ID_LAST);
        world_set_block(w, cells[i], ids[i]);
    }
    for (int i = 299; i >= 0; i--) {
        /* the last write to a cell wins */
        bool overwritten = false;
        for (int j = i + 1; j < 300; j++)
            if (cells[j].x == cells[i].x && cells[j].y == cells[i].y && cells[j].z == cells[i].z) overwritten = true;
        if (!overwritten) CHECK_EQ(world_get_block(w, cells[i]), ids[i]);
    }
    size_t non_air = 0;
    for (int x = -32; x < 32; x++)
        for (int y = -32; y < 32; y++)
            for (int z = -32; z < 32; z++) non_air += world_get_block(w, V(x, y, z)) != AIR;
    CHECK(non_air <= 300);
    tw_destroy(w);
}

TEST(unloaded_positions_read_as_empty_and_ignore_writes) {
    struct World *w = tw_create(V(0, 0, 0), 2, 1);
    tw_load_empty(w, V(0, 0, 0)); /* only one of the 8 slots loaded */
    ivec3s unloaded = V(-5, 5, 5), out_of_range = V(1000, 0, 0);
    CHECK(world_in_bounds(w, unloaded));
    CHECK(!world_contains(w, unloaded));
    CHECK(!world_in_bounds(w, out_of_range));
    world_set_block(w, unloaded, STONE);
    world_set_block(w, out_of_range, STONE);
    CHECK_EQ(world_get_block(w, unloaded), AIR);
    CHECK_EQ(world_get_block(w, out_of_range), AIR);
    CHECK_EQ(world_get_light(w, unloaded), 0);
    world_set_block(w, V(5, 5, 5), STONE);
    CHECK_EQ(world_get_block(w, V(5, 5, 5)), STONE);
    tw_destroy(w);
}

/* ---- coordinate mapping properties ------------------------------------- */

TEST(block_position_decomposes_into_chunk_offset_and_local_position) {
    test_rng_seed(11);
    for (int i = 0; i < 20000; i++) {
        ivec3s p = V(test_rng_range(-5000, 5000), test_rng_range(-300, 300), test_rng_range(-5000, 5000));
        ivec3s o = world_pos_to_offset(p), l = world_pos_to_chunk_pos(p);
        CHECK(chunk_in_bounds(l));
        CHECK_IVEC3_EQ(tw_pos(o, l), p);
    }
}

TEST(float_position_maps_to_containing_block) {
    test_rng_seed(12);
    for (int i = 0; i < 20000; i++) {
        vec3s f = {{ (float)test_rng_range(-400000, 400000) / 100.0f + 0.005f,
                     (float)test_rng_range(-40000, 40000) / 100.0f + 0.005f,
                     (float)test_rng_range(-400000, 400000) / 100.0f + 0.005f }};
        ivec3s b = world_pos_to_block(f);
        CHECK(b.x <= f.x && f.x < b.x + 1);
        CHECK(b.y <= f.y && f.y < b.y + 1);
        CHECK(b.z <= f.z && f.z < b.z + 1);
    }
}

TEST(chunk_slot_index_is_a_bijection_and_contains_matches_loaded_chunks) {
    for (size_t size = 1; size <= 4; size++) {
        struct World *w = tw_create(V(-70, 40, 100), size, 1);
        size_t n = size * size * size;
        for (size_t i = 0; i < n; i++) {
            ivec3s o = world_chunk_offset(w, i);
            CHECK(world_chunk_in_bounds(w, o));
            CHECK_EQ(world_chunk_index(w, o), i);
            CHECK(!world_contains_chunk(w, o));
        }
        tw_fill_empty(w);
        for (size_t i = 0; i < n; i++) {
            ivec3s o = world_chunk_offset(w, i);
            struct Chunk *c = world_get_chunk(w, o);
            CHECK(c != NULL);
            CHECK_IVEC3_EQ(c->offset, o);
        }
        /* the loaded region is exactly size^3 chunks around the centre chunk */
        ivec3s centre = world_pos_to_offset(V(-70, 40, 100));
        for (int dx = -3; dx <= 3; dx++)
            for (int dy = -3; dy <= 3; dy++)
                for (int dz = -3; dz <= 3; dz++) {
                    ivec3s o = V(centre.x + dx, centre.y + dy, centre.z + dz);
                    size_t inside = 0;
                    for (size_t i = 0; i < n; i++) {
                        ivec3s s = world_chunk_offset(w, i);
                        inside += s.x == o.x && s.y == o.y && s.z == o.z;
                    }
                    CHECK_EQ(world_contains_chunk(w, o), inside == 1);
                }
        CHECK(world_contains_chunk(w, centre));
        tw_destroy(w);
    }
}

int main(int argc, char **argv) { return run_tests(argc, argv); }
