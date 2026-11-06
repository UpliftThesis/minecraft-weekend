/* Known defects in the current code, kept visible across refactoring.
 *
 * Every test here asserts the behaviour the game *should* have. Each one fails
 * against the current code and is reported as XFAIL. When a refactoring (or a
 * deliberate fix) makes one pass, the runner reports XPASS and fails, so the
 * change is noticed and the KNOWN_DEFECT is converted into an ordinary TEST.
 *
 * Do not "fix" these tests to match the current output.
 */
#include "check.h"
#include "harness.h"

#define V(x, y, z) ((ivec3s){{ (x), (y), (z) }})

/* An all-air world: one chunk (0,0,0) under open sky, so every cell has
 * sunlight LIGHT_MAX and no torchlight. */
static struct World *open_air_world(void) {
    struct World *w = tw_create(V(16, 16, 16), 1, 1);
    tw_fill_empty(w);
    return w;
}

/* ---- Defect 1: torchlight writes wipe the upper half of the cell ----------
 * The chunk cell packs block, torchlight, sunlight and metadata into one u64.
 * TORCHLIGHT_MASK (src/world/chunk.h) is written 0x000000000FFFF0000, a literal
 * that fits in 32 bits, so the complement used when writing the field is also
 * 32 bits wide and clears bits 32..63: sunlight and metadata are lost. */

KNOWN_DEFECT(torchlight_write_preserves_sunlight,
             "TORCHLIGHT_MASK is a 32-bit literal; writing torchlight clears sunlight in the same cell") {
    struct World *w = open_air_world();
    ivec3s p = V(10, 10, 10);
    world_set_sunlight(w, p, 9);
    world_set_torchlight(w, p, TORCHLIGHT_OF(1, 2, 3, 4));
    CHECK_EQ(world_get_torchlight(w, p), TORCHLIGHT_OF(1, 2, 3, 4));
    CHECK_EQ(world_get_sunlight(w, p), 9);
    tw_destroy(w);
}

KNOWN_DEFECT(torchlight_write_preserves_metadata,
             "TORCHLIGHT_MASK is a 32-bit literal; writing torchlight clears metadata in the same cell") {
    struct World *w = open_air_world();
    ivec3s p = V(10, 10, 10);
    world_set_metadata(w, p, 5);
    world_set_torchlight(w, p, TORCHLIGHT_OF(1, 2, 3, 4));
    CHECK_EQ(world_get_metadata(w, p), 5);
    tw_destroy(w);
}

/* The same defect seen from gameplay: when a chunk containing a light source
 * is generated, the lighting pass stores the source's torchlight after giving
 * the cell full sunlight, so a torch standing under open sky ends up with
 * sunlight 0 in its own cell (the neighbouring air keeps LIGHT_MAX). */
KNOWN_DEFECT(generated_torch_under_open_sky_keeps_sunlight,
             "light_apply writes the torch's torchlight after its sunlight; the torchlight write clears sunlight") {
    struct World *w = tw_create(V(16, 16, 16), 1, 1);
    const struct TWBlock blocks[] = { { V(16, 16, 16), TORCH } };
    tw_load_prefilled(w, V(0, 0, 0), blocks, 1);
    CHECK_EQ(world_get_sunlight(w, V(17, 16, 16)), LIGHT_MAX); /* sanity: open sky */
    CHECK_EQ(world_get_sunlight(w, V(16, 16, 16)), LIGHT_MAX);
    tw_destroy(w);
}

/* ---- Defect 2: metadata field narrower than documented --------------------
 * chunk.h documents 28 bits of metadata at offset 36, but METADATA_MASK is
 * 0xFFFFFF7000000000: bit 39 (bit 3 of the field) is missing, so any value with
 * bit 3 set does not round-trip. */
KNOWN_DEFECT(metadata_roundtrips_values_with_bit3_set,
             "METADATA_MASK is 0xFFFFFF7...: bit 3 of the 28-bit metadata field is dropped") {
    struct World *w = open_air_world();
    ivec3s p = V(3, 4, 5);
    const u32 values[] = { 8, 15, 0x123, 0xFFFFFFF };
    for (size_t i = 0; i < sizeof values / sizeof values[0]; i++) {
        world_set_metadata(w, p, values[i]);
        CHECK_MSG(world_get_metadata(w, p) == values[i], "metadata 0x%x read back as 0x%x",
                  values[i], world_get_metadata(w, p));
    }
    tw_destroy(w);
}

/* ---- Defect 3: generation-time light sources only spread one channel ------
 * light_apply (src/world/light.c) builds a queue of light sources but, when
 * propagating each colour channel, reads `torchlight_queue.elements[i]` (the
 * channel index) instead of `elements[j]`. A torch that exists when its chunk
 * is generated therefore only spreads its intensity channel; red/green/blue
 * stay at 0 next to it. A torch placed after loading spreads all channels, so
 * both should produce the same light. */
KNOWN_DEFECT(generated_torch_spreads_all_colour_channels,
             "light_apply indexes the source queue with the channel index i instead of j") {
    harness_init();
    Torchlight torch = BLOCKS[TORCH].get_torchlight(NULL, V(0, 0, 0));
    struct World *w = tw_create(V(16, 16, 16), 1, 1);
    const struct TWBlock blocks[] = { { V(16, 16, 16), TORCH } };
    tw_load_prefilled(w, V(0, 0, 0), blocks, 1);
    Torchlight n = world_get_torchlight(w, V(17, 16, 16));
    CHECK_EQ(TORCHLIGHT_I(n), TORCHLIGHT_I(torch) - 1);
    CHECK_EQ(TORCHLIGHT_R(n), TORCHLIGHT_R(torch) - 1);
    CHECK_EQ(TORCHLIGHT_G(n), TORCHLIGHT_G(torch) - 1);
    CHECK_EQ(TORCHLIGHT_B(n), TORCHLIGHT_B(torch) - 1);
    tw_destroy(w);
}

/* ---- Defect 4: the chunk above is not re-meshed after a top-layer edit -----
 * chunk_on_modify marks neighbouring chunks dirty when a boundary block
 * changes, but chunk_get_bordering_chunks tests `pos.x == CHUNK_SIZE.y - 1`
 * for the +Y neighbour instead of pos.y. Editing the top layer of a chunk does
 * not re-mesh the chunk above (whose bottom faces may become visible/hidden),
 * while editing the bottom layer does re-mesh the chunk below. */
KNOWN_DEFECT(editing_top_layer_remeshes_chunk_above,
             "chunk_get_bordering_chunks checks pos.x instead of pos.y for the +Y neighbour") {
    struct World *w = tw_create(V(0, 0, 0), 2, 1);
    tw_fill_empty(w);
    struct Chunk *below = world_get_chunk(w, V(0, -1, 0)), *above = world_get_chunk(w, V(0, 0, 0));
    CHECK(below && above);
    tw_chunk_mesh_clear_dirty(below);
    tw_chunk_mesh_clear_dirty(above);
    world_set_block(w, V(5, -1, 5), STONE); /* top layer of the lower chunk */
    CHECK(tw_chunk_mesh_dirty(below));
    CHECK(tw_chunk_mesh_dirty(above));
    tw_destroy(w);
}

/* Control for the test above (an ordinary test): the -Y direction works. */
TEST(editing_bottom_layer_remeshes_chunk_below) {
    struct World *w = tw_create(V(0, 0, 0), 2, 1);
    tw_fill_empty(w);
    struct Chunk *below = world_get_chunk(w, V(0, -1, 0)), *above = world_get_chunk(w, V(0, 0, 0));
    tw_chunk_mesh_clear_dirty(below);
    tw_chunk_mesh_clear_dirty(above);
    world_set_block(w, V(5, 0, 5), STONE); /* bottom layer of the upper chunk */
    CHECK(tw_chunk_mesh_dirty(above));
    CHECK(tw_chunk_mesh_dirty(below));
    tw_destroy(w);
}

int main(int argc, char **argv) { return run_tests(argc, argv); }
