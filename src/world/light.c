#include "light.h"
#include "../block/block.h"
#include "chunk.h"
#include "world.h"

// TODO: there is currently a sunlight bug where sunlight which is propagating
// down in a column which is not its original column still maintains its value
// as it goes lower - this is something optional to fix tha probably (?) should
// be fixed.

#define QUEUE_SIZE 65536 

#define ENQUEUE(q, e)\
    assert(((q)->size + 1) < QUEUE_SIZE);\
    ((q)->elements[(q)->size++] = (e));

#define DEQUEUE(q) ((q)->elements[--(q)->size])

struct LightNode {
    ivec3s pos;
    u32 value;
};

struct LightQueue {
    struct LightNode elements[QUEUE_SIZE];
    size_t size;
};

enum PropagationType {
    DEFAULT_LIGHT, SUNLIGHT
};

// Propagate DEFAULT_LIGHT through the queue.
static void add_default_propagate(
    struct World *world, struct LightQueue *queue,
    u32 mask, u32 offset) {
    while (queue->size != 0) {
        struct LightNode node = DEQUEUE(queue);
        u32 light = world_get_light(world, node.pos),
            val = (light & mask) >> offset;

        // propagate in reverse of enum Direction order so DOWN is first
        for (enum Direction d = DOWN; (s32) d >= 0; d--) {
            ivec3s n_pos = glms_ivec3_add(node.pos, DIR2IVEC3S(d));
            u64 n_data = world_get_data(world, n_pos);
            u32 n_light = chunk_data_to_light(n_data),
                n_val = ((n_light & mask) >> offset);
            struct Block n_block = BLOCKS[chunk_data_to_block(n_data)];

            // TODO: it may be incorrect to check n_val != 0 to subvert the block behavior call
            if ((n_val == 0 && n_block.transparent == false)) continue;
            if (n_val + 1 >= val) continue;

            world_set_light(
                world, n_pos,
                ((n_light & ~mask) | (((val - 1) << offset) & mask)));
            ENQUEUE(queue, ((struct LightNode){.pos=n_pos}));
        }
    }
}

// Handle a DOWN neighbour during sunlight addition (no dimming).
static void handle_down_sunlight(
    struct World *world, struct LightQueue *queue,
    ivec3s pos, u32 val, u32 mask, u32 offset) {
    ivec3s n_pos = glms_ivec3_add(pos, DIR2IVEC3S(DOWN));
    u64 n_data = world_get_data(world, n_pos);
    u32 n_light = chunk_data_to_light(n_data),
        n_val = ((n_light & mask) >> offset);
    struct Block n_block = BLOCKS[chunk_data_to_block(n_data)];

    if (!(n_val != 0 || n_block.transparent)) return;
    if (n_val >= val) return;

    world_set_light(world, n_pos,
                    ((n_light & ~mask) | ((val << offset) & mask)));
    ENQUEUE(queue, ((struct LightNode){.pos=n_pos}));
}

// Handle a side neighbour during sunlight addition (dimming by 1).
static void handle_side_sunlight(
    struct World *world, struct LightQueue *queue,
    enum Direction d, ivec3s pos, u32 val, u32 mask, u32 offset) {
    ivec3s n_pos = glms_ivec3_add(pos, DIR2IVEC3S(d));
    u64 n_data = world_get_data(world, n_pos);
    u32 n_light = chunk_data_to_light(n_data),
        n_val = ((n_light & mask) >> offset);
    struct Block n_block = BLOCKS[chunk_data_to_block(n_data)];

    if (n_val == 0 && !n_block.transparent) return;
    if (n_val + 1 >= val) return;

    world_set_light(world, n_pos,
                    ((n_light & ~mask) | (((val - 1) << offset) & mask)));
    ENQUEUE(queue, ((struct LightNode){.pos=n_pos}));
}

// Propagate SUNLIGHT through the queue (no dimming during downward travel).
static void add_sunlight_propagate(
    struct World *world, struct LightQueue *queue,
    u32 mask, u32 offset) {
    while (queue->size != 0) {
        struct LightNode node = DEQUEUE(queue);
        u32 light = world_get_light(world, node.pos),
            val = (light & mask) >> offset;

        // propagate in reverse of enum Direction order so DOWN is first
        for (enum Direction d = DOWN; (s32)d >= 0; d--) {
            ivec3s n_pos = glms_ivec3_add(node.pos, DIR2IVEC3S(d));
            u64 n_data = world_get_data(world, n_pos);
            u32 n_light = chunk_data_to_light(n_data),
                n_val = ((n_light & mask) >> offset);
            struct Block n_block = BLOCKS[chunk_data_to_block(n_data)];

            if (d == DOWN) {
                // DOWN propagates without dimming
                const bool allows_flow = n_val != 0 || n_block.transparent;
                if (allows_flow && n_val < val) {
                    world_set_light(
                        world, n_pos,
                        ((n_light & ~mask) | ((val << offset) & mask)));
                    ENQUEUE(queue, ((struct LightNode){.pos=n_pos}));
                }
            } else {
                // Side neighbours propagate with dimming by 1
                if (n_val == 0 && !n_block.transparent) continue;
                if (n_val + 1 >= val) continue;

                world_set_light(
                    world, n_pos,
                    ((n_light & ~mask) | (((val - 1) << offset) & mask)));
                ENQUEUE(queue, ((struct LightNode){.pos=n_pos}));
            }
        }
    }
}

// Route add_propagate to the sunlight or default variant.
static void add_propagate(
    struct World *world, struct LightQueue *queue,
    u32 mask, u32 offset, enum PropagationType type) {
    if (type == SUNLIGHT)
        add_sunlight_propagate(world, queue, mask, offset);
    else
        add_default_propagate(world, queue, mask, offset);
}

// Remove a light channel at a position and re-propagate surrounding values.
// For SUNLIGHT, downward neighbours are always cleared regardless of value.
static void remove_default_propagate(
    struct World *world, struct LightQueue *queue, struct LightQueue *prop_queue,
    u32 mask, u32 offset) {
    while (queue->size != 0) {
        struct LightNode node = DEQUEUE(queue);
        u32 value = node.value;

        for (enum Direction d = 0; d < 6; d++) {
            ivec3s n_pos = glms_ivec3_add(node.pos, DIR2IVEC3S(d));
            u32 n_light = world_get_light(world, n_pos),
                n_value = (n_light & mask) >> offset;

            if ((n_light & mask) == 0) continue;

            if (n_value < value) {
                world_set_light(world, n_pos, n_light & ~mask);
                ENQUEUE(queue, ((struct LightNode){.pos=n_pos, .value=n_value}));
                continue;
            }

            if (n_value >= value) {
                ENQUEUE(prop_queue, ((struct LightNode){.pos=n_pos}));
            }
        }
    }
}

// Handle a DOWN neighbour during sunlight removal (always clears).
static void handle_down_sunlight_remove(
    struct World *world, struct LightQueue *queue,
    ivec3s pos, u32 value, u32 mask, u32 offset) {
    ivec3s n_pos = glms_ivec3_add(pos, DIR2IVEC3S(DOWN));
    u32 n_light = world_get_light(world, n_pos),
        n_value = (n_light & mask) >> offset;

    if ((n_light & mask) == 0) return;

    world_set_light(world, n_pos, n_light & ~mask);
    ENQUEUE(queue, ((struct LightNode){.pos=n_pos, .value=n_value}));
}

// Handle a side neighbour during sunlight removal.
static void handle_side_sunlight_remove(
    struct World *world, struct LightQueue *queue, struct LightQueue *prop_queue,
    enum Direction d, ivec3s pos, u32 value, u32 mask, u32 offset) {
    ivec3s n_pos = glms_ivec3_add(pos, DIR2IVEC3S(d));
    u32 n_light = world_get_light(world, n_pos),
        n_value = (n_light & mask) >> offset;

    if ((n_light & mask) == 0) return;

    if (n_value < value) {
        world_set_light(world, n_pos, n_light & ~mask);
        ENQUEUE(queue, ((struct LightNode){.pos=n_pos, .value=n_value}));
        return;
    }

    ENQUEUE(prop_queue, ((struct LightNode){.pos=n_pos}));
}

static void remove_sunlight_propagate(
    struct World *world, struct LightQueue *queue, struct LightQueue *prop_queue,
    u32 mask, u32 offset) {
    while (queue->size != 0) {
        struct LightNode node = DEQUEUE(queue);
        u32 value = node.value;

        for (enum Direction d = 0; d < 6; d++) {
            if (d == DOWN) {
                handle_down_sunlight_remove(world, queue, node.pos, value, mask, offset);
            } else {
                handle_side_sunlight_remove(world, queue, prop_queue, d, node.pos, value, mask, offset);
            }
        }
    }
}

// Route remove_propagate to the sunlight or default variant.
static void remove_propagate(
    struct World *world, struct LightQueue *queue, struct LightQueue *prop_queue,
    u32 mask, u32 offset, enum PropagationType type) {
    if (type == SUNLIGHT)
        remove_sunlight_propagate(world, queue, prop_queue, mask, offset);
    else
        remove_default_propagate(world, queue, prop_queue, mask, offset);
}

static void add_channel(
    struct World *world, ivec3s pos,
    u8 value, u32 mask, u32 offset, enum PropagationType type) {
    struct LightQueue queue = { .size = 0 };
    world_set_light(world, pos, (world_get_light(world, pos) & ~mask) | (((u32) value) << offset));
    ENQUEUE(&queue, ((struct LightNode) { .pos = pos }));
    add_propagate(world, &queue, mask, offset, type);
}

static void remove_channel(
    struct World *world, ivec3s pos,
    u32 mask, u32 offset, enum PropagationType type) {
    struct LightQueue
        queue = { .size = 0 },
        prop_queue = { .size = 0 };

    u32 light = world_get_light(world, pos); 
    world_set_light(world, pos, light & ~mask);

    ENQUEUE(&queue, ((struct LightNode) { .pos = pos, .value = (light & mask) >> offset }));
    remove_propagate(world, &queue, &prop_queue, mask, offset, type);
    add_propagate(world, &prop_queue, mask, offset, type);
}

// Enqueue sunlight neighbour positions for side-propagation from one column cell.
// Called when the cell is above the local heightmap; checks four cardinal neighbours.
static void enqueue_sunlight_neighbors(
    struct LightQueue *sunlight_queue, struct Chunk *chunk,
    ivec3s pos_c, ivec3s pos_w) {
    for (enum Direction d = NORTH; d <= WEST; d++) {
        ivec3s d_v = DIR2IVEC3S(d);
        ivec3s pos_cn = glms_ivec3_add(pos_c, d_v);

        if (!chunk_in_bounds(pos_cn)) {
            s64 neighbor_h = world_heightmap_get(
                chunk->world,
                ((ivec2s){{pos_w.x + d_v.x, pos_w.z + d_v.z}}));
            if (pos_w.y < neighbor_h) {
                ENQUEUE(sunlight_queue,
                        ((struct LightNode){.pos = pos_w}));
            }
            continue;
        }

        s64 neighbor_h = HEIGHTMAP_GET(
            chunk_get_heightmap(chunk),
            ((ivec2s){{pos_cn.x, pos_cn.z}}));
        if (pos_w.y < neighbor_h) {
            ENQUEUE(sunlight_queue,
                    ((struct LightNode){.pos = pos_w}));
        }
    }
}

// Scan one column for sunlight exposure and torchlight-emitting blocks.
static void process_column_sunlight_and_sources(
    struct Chunk *chunk, struct Heightmap *heightmap,
    s64 local_x, s64 local_z,
    struct LightQueue *sunlight_queue, struct LightQueue *torchlight_queue) {
    s64 h = HEIGHTMAP_GET(heightmap, ((ivec2s){{local_x, local_z}}));

    for (s64 y = CHUNK_SIZE.y - 1; y >= 0; y--) {
        ivec3s pos_c = (ivec3s){{local_x, y, local_z}};
        ivec3s pos_w = glms_ivec3_add(chunk->position, pos_c);

        if (pos_w.y > h) {
            chunk_set_sunlight(chunk, pos_c, LIGHT_MAX);
            enqueue_sunlight_neighbors(sunlight_queue, chunk, pos_c, pos_w);
        }

        struct Block block = BLOCKS[chunk_get_block(chunk, pos_c)];
        if (block.can_emit_light) {
            Torchlight value = block.get_torchlight(chunk->world, pos_w);
            chunk_set_torchlight(chunk, pos_c, value);
            ENQUEUE(torchlight_queue, ((struct LightNode){.pos=pos_w, .value=value}));
        }
    }
}

// Scan two opposite chunk faces and enqueue non-zero torchlight from adjacent
// chunk cells.  fixed_dim selects the axis; dim_a/dim_b are the iteration axes.
static void enqueue_chunk_faces(
    struct Chunk *chunk, struct LightQueue *torchlight_queue,
    int fixed_dim,       // 0=X, 1=Y, 2=Z
    s64 offset_lo, s64 offset_hi,
    int dim_a, int dim_b,
    size_t size_a, size_t size_b) {
    for (size_t a = 0; a < size_a; a++) {
        for (size_t b = 0; b < size_b; b++) {
            s64 coords_lo[3] = {0};
            s64 coords_hi[3] = {0};
            coords_lo[dim_a] = a;     coords_lo[dim_b] = b;     coords_lo[fixed_dim] = offset_lo;
            coords_hi[dim_a] = a;     coords_hi[dim_b] = b;     coords_hi[fixed_dim] = offset_hi;

            ivec3s wp_lo = glms_ivec3_add(chunk->position,
                    (ivec3s){{coords_lo[0], coords_lo[1], coords_lo[2]}});
            u32 l = world_get_light(chunk->world, wp_lo);
            if (l != 0) {
                ENQUEUE(torchlight_queue, ((struct LightNode){.pos=wp_lo, .value=l}));
            }

            ivec3s wp_hi = glms_ivec3_add(chunk->position,
                    (ivec3s){{coords_hi[0], coords_hi[1], coords_hi[2]}});
            l = world_get_light(chunk->world, wp_hi);
            if (l != 0) {
                ENQUEUE(torchlight_queue, ((struct LightNode){.pos=wp_hi, .value=l}));
            }
        }
    }
}

// Enqueue torchlight sources on two opposite chunk faces (bottom + top).
static void enqueue_face_bottom_top(
    struct Chunk *chunk, struct LightQueue *torchlight_queue) {
    enqueue_chunk_faces(chunk, torchlight_queue, 1, -1, CHUNK_SIZE.y, 0, 2,
                        CHUNK_SIZE.x, CHUNK_SIZE.z);
}

// Enqueue torchlight sources on two opposite chunk faces (front + back).
static void enqueue_face_front_back(
    struct Chunk *chunk, struct LightQueue *torchlight_queue) {
    enqueue_chunk_faces(chunk, torchlight_queue, 2, -1, CHUNK_SIZE.z, 0, 1,
                        CHUNK_SIZE.x, CHUNK_SIZE.y);
}

// Enqueue torchlight sources on two opposite chunk faces (left + right).
static void enqueue_face_left_right(
    struct Chunk *chunk, struct LightQueue *torchlight_queue) {
    enqueue_chunk_faces(chunk, torchlight_queue, 0, -1, CHUNK_SIZE.x, 1, 2,
                        CHUNK_SIZE.y, CHUNK_SIZE.z);
}

void torchlight_add(struct World *world, ivec3s pos, Torchlight light) {
    if (!BLOCKS[world_get_block(world, pos)].transparent) {
        return;
    }

    for (size_t i = 0; i < 4; i++) {
        u32 mask = 0xF << (i * 4), offset = i * 4;
        add_channel(world, pos, (light & mask) >> offset, mask, offset, DEFAULT_LIGHT);
    }
}

void torchlight_remove(struct World *world, ivec3s pos) {
    for (size_t i = 0; i < 4; i++) {
        remove_channel(world, pos, 0xF << (i * 4), i * 4, DEFAULT_LIGHT);
    }
}

// updates lighting at the specified position, propagating light around it
void light_update(struct World *world, ivec3s pos) {
    struct LightQueue queue = { .size = 0 }; 

    // i is [0..4] instead of [0..3] so sunlight is propagated as well
    for (size_t i = 0; i < 5; i++) {
        u32 mask = 0xF << (i * 4), offset = i * 4;
        bool sunlight = i == 4;
        queue.size = 0;

        for (enum Direction d = 0; d < 6; d++) {
            ivec3s pos_n = glms_ivec3_add(pos, DIR2IVEC3S(d));

            if (!BLOCKS[world_get_block(world, pos_n)].transparent) {
                continue;
            }

            ENQUEUE(&queue, ((struct LightNode) { .pos = pos_n }));
        }

        // enqueue for sunlight if this block is above the heightmap
        if (sunlight &&
            pos.y > world_heightmap_get(world, (ivec2s) {{ pos.x, pos.z }})) {
            world_set_sunlight(world, pos, LIGHT_MAX);
            ENQUEUE(&queue, ((struct LightNode) { .pos = pos }));
        }

        add_propagate(world, &queue, mask, offset, sunlight ? SUNLIGHT : DEFAULT_LIGHT);
    }
}

void light_remove(struct World *world, ivec3s pos) {
    torchlight_remove(world, pos);
    remove_channel(world, pos, 0xF0000, 16, SUNLIGHT);
}

// Build a source list for one torchlight channel from a set of positions.
// Uses fixed_idx rather than the inner-loop variable to preserve existing
// cross-channel propagation behaviour (each channel checks the same set of
// initial positions).
static void build_channel_propagation_sources(
    struct LightQueue *target, const struct LightQueue *source,
    size_t fixed_idx, u32 mask) {
    for (size_t j = 0; j < source->size; j++) {
        struct LightNode n = source->elements[fixed_idx];
        if ((n.value & mask) != 0) {
            ENQUEUE(target, ((struct LightNode){.pos=n.pos}));
        }
    }
}

void light_apply(struct Chunk *chunk) {
    struct Heightmap *heightmap = chunk_get_heightmap(chunk);
    struct LightQueue sunlight_queue = { .size = 0 },
        torchlight_queue = { .size = 0 };

    // propagate sunlight for this chunk
    for (s64 x = 0; x < CHUNK_SIZE.x; x++) {
        for (s64 z = 0; z < CHUNK_SIZE.z; z++) {
            process_column_sunlight_and_sources(chunk, heightmap, x, z,
                                                &sunlight_queue, &torchlight_queue);
        }
    }

    add_propagate(chunk->world, &sunlight_queue, 0xF0000, 16, SUNLIGHT);

    // enqueue all non-empty light values from adjacent-chunk faces
    enqueue_face_bottom_top(chunk, &torchlight_queue);
    enqueue_face_front_back(chunk, &torchlight_queue);
    enqueue_face_left_right(chunk, &torchlight_queue);

    // propagate each channel if the channel's value in the border position is
    // nonzero
    struct LightQueue queue = { .size = 0 };
    for (size_t i = 0; i < 4; i++) {
        u32 mask = 0xF << (i * 4), offset = i * 4;
        queue.size = 0;
        build_channel_propagation_sources(&queue, &torchlight_queue, i, mask);
        add_propagate(chunk->world, &queue, mask, offset, DEFAULT_LIGHT);
    }
}
