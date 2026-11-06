/* Helpers that build small, deterministic, headless worlds.
 *
 * All knowledge of World/Chunk construction internals used by the tests is
 * concentrated here, so a refactoring that changes how worlds are assembled
 * should only require updating this file, not the individual tests.
 */
#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include "state.h"
#include "fake_platform.h"

/* Install fake GL and register all blocks. Idempotent; call at the top of a test. */
void harness_init(void);

/* A world of chunks_size^3 chunk slots centred on the chunk containing
 * `center_block`, with no chunks loaded yet. Heightmaps exist for every
 * column and are initially unknown. */
struct World *tw_create(ivec3s center_block, size_t chunks_size, u64 seed);

/* Load every empty slot with an all-AIR chunk (no terrain generation),
 * running the same post-generation steps (heightmap, lighting) as the game. */
void tw_fill_empty(struct World *w);

/* Load one chunk slot with an all-AIR chunk (see tw_fill_empty). */
struct Chunk *tw_load_empty(struct World *w, ivec3s chunk_offset);

/* A block placed while a chunk is being generated (local chunk position). */
struct TWBlock { ivec3s local; enum BlockId block; };

/* Load one chunk slot whose content is `blocks` (everything else AIR), placed
 * the way the terrain generator places blocks, i.e. before the chunk's
 * post-generation heightmap and lighting pass. */
struct Chunk *tw_load_prefilled(struct World *w, ivec3s chunk_offset, const struct TWBlock *blocks, size_t n);

/* True if the chunk has been flagged as needing its mesh rebuilt. */
bool tw_chunk_mesh_dirty(struct Chunk *c);
void tw_chunk_mesh_clear_dirty(struct Chunk *c);

/* Load one chunk slot through the game's own terrain generator. */
struct Chunk *tw_load_generated(struct World *w, ivec3s chunk_offset);

/* Load every empty slot through the terrain generator, nearest-to-centre first. */
void tw_fill_generated(struct World *w);

/* Number of loaded chunks. */
size_t tw_loaded_count(struct World *w);

/* Release everything owned by the world, including the struct itself. */
void tw_destroy(struct World *w);

/* Convenience: block position of chunk offset `o` plus local position `p`. */
static inline ivec3s tw_pos(ivec3s chunk_offset, ivec3s local) {
    return (ivec3s){{ chunk_offset.x * CHUNK_SIZE_X + local.x,
                      chunk_offset.y * CHUNK_SIZE_Y + local.y,
                      chunk_offset.z * CHUNK_SIZE_Z + local.z }};
}

#endif
