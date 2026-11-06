#include "harness.h"
#include "block/block.h"

/* Defined in world.c without a header declaration. */
void world_load_chunk(struct World *self, ivec3s offset);

static bool harness_ready;

void harness_init(void) {
    if (harness_ready) return;
    harness_ready = true;
    fake_gl_install();
    block_init();
    srand(12345);
}

struct World *tw_create(ivec3s center_block, size_t chunks_size, u64 seed) {
    harness_init();
    struct World *w = calloc(1, sizeof *w);
    ecs_init(&w->ecs, w);
    sky_init(&w->sky, w);
    w->seed = seed;
    w->throttles.mesh.max = 8;
    w->throttles.load.max = 0; /* do not load anything during centring */
    w->unloaded_blocks.capacity = 64;
    w->unloaded_blocks.list = malloc(w->unloaded_blocks.capacity * sizeof *w->unloaded_blocks.list);
    w->chunks_size = chunks_size;
    w->chunks = calloc(chunks_size * chunks_size * chunks_size, sizeof(struct Chunk *));
    w->heightmaps = calloc(chunks_size * chunks_size, sizeof(struct Heightmap *));
    /* any origin different from the one world_set_center will compute */
    w->chunks_origin = (ivec3s){{ 1 << 20, 1 << 20, 1 << 20 }};
    world_set_center(w, center_block);
    return w;
}

struct Chunk *tw_load_empty(struct World *w, ivec3s offset) {
    return tw_load_prefilled(w, offset, NULL, 0);
}

struct Chunk *tw_load_prefilled(struct World *w, ivec3s offset, const struct TWBlock *blocks, size_t n) {
    struct Chunk *c = malloc(sizeof *c);
    chunk_init(c, w, offset);
    c->flags.generating = true;
    for (size_t i = 0; i < n; i++) chunk_set_block(c, blocks[i].local, blocks[i].block);
    c->flags.generating = false;
    w->chunks[world_chunk_index(w, offset)] = c;
    chunk_after_generate(c);
    return c;
}

bool tw_chunk_mesh_dirty(struct Chunk *c) { return c->mesh->flags.dirty; }
void tw_chunk_mesh_clear_dirty(struct Chunk *c) { c->mesh->flags.dirty = false; }

struct Chunk *tw_load_generated(struct World *w, ivec3s offset) {
    world_load_chunk(w, offset);
    return world_get_chunk(w, offset);
}

static size_t num_chunks(struct World *w) { return w->chunks_size * w->chunks_size * w->chunks_size; }

void tw_fill_empty(struct World *w) {
    for (size_t i = 0; i < num_chunks(w); i++)
        if (!w->chunks[i]) tw_load_empty(w, world_chunk_offset(w, i));
}

void tw_fill_generated(struct World *w) {
    size_t n = w->chunks_size;
    for (size_t i = 0; i < n * n * n; i++) {
        ivec3s o = world_chunk_offset(w, i);
        if (!world_get_chunk(w, o)) world_load_chunk(w, o);
    }
}

size_t tw_loaded_count(struct World *w) {
    size_t c = 0;
    for (size_t i = 0; i < num_chunks(w); i++) c += w->chunks[i] != NULL;
    return c;
}

void tw_destroy(struct World *w) {
    for (size_t i = 0; i < w->chunks_size * w->chunks_size; i++) {
        struct Heightmap *h = w->heightmaps[i];
        if (h) { free(h->data); free(h->worldgen_data); free(h); w->heightmaps[i] = NULL; }
    }
    /* chunk meshes are released by world_destroy but their structs are not freed */
    size_t n = num_chunks(w), k = 0;
    struct ChunkMesh **meshes = calloc(n ? n : 1, sizeof *meshes);
    for (size_t i = 0; i < n; i++) if (w->chunks[i]) meshes[k++] = w->chunks[i]->mesh;
    free(w->unloaded_blocks.list);
    world_destroy(w);
    for (size_t i = 0; i < k; i++) free(meshes[i]);
    free(meshes);
    free(w);
}
