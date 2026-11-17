
#include "worldgen.h"
#include "noise.h"
#include "../chunk.h"
#include "../world.h"

#define RADIAL2I(c, r, v)\
    (glms_vec2_norm(glms_vec2_sub(IVEC2S2V((c)), IVEC2S2V((v)))) / glms_vec2_norm(IVEC2S2V((r))))

#define RADIAL3I(c, r, v)\ 
    (glms_vec3_norm(glms_vec3_sub(IVEC3S2V((c)), IVEC3S2V((v)))) / glms_vec3_norm(IVEC3S2V((r))))

#define WATER_LEVEL 0

#define BIOME_LAST MOUNTAIN
enum Biome {
    OCEAN,
    RIVER,
    BEACH,
    DESERT,
    SAVANNA,
    JUNGLE,
    GRASSLAND,
    WOODLAND,
    FOREST,
    RAINFOREST,
    TAIGA,
    TUNDRA,
    ICE,
    MOUNTAIN
};

#define MAX_DECORATIONS 8
struct Decoration {
    FWGDecorate f;
    f32 chance;
};

struct BiomeData {
    enum BlockId top_block, bottom_block;
    f32 roughness, scale, exp;
    struct Decoration decorations[MAX_DECORATIONS];
};

struct BiomeData BIOME_DATA[BIOME_LAST + 1] = {
    [OCEAN] = {
        .top_block = SAND,
        .bottom_block = SAND,
        .roughness = 1.0f,
        .scale = 1.0f,
        .exp = 1.0f
    },
    [RIVER] = {
        .top_block = SAND,
        .bottom_block = SAND,
        .roughness = 1.0f,
        .scale = 1.0f,
        .exp = 1.0f
    },
    [BEACH] = {
        .top_block = SAND,
        .bottom_block = SAND,
        .roughness = 0.2f,
        .scale = 0.8f,
        .exp = 1.3f
    },
    [DESERT] = {
        .top_block = SAND,
        .bottom_block = SAND,
        .roughness = 0.6f,
        .scale = 0.6f,
        .exp = 1.2f,
        .decorations = {
            { .f = worldgen_shrub, .chance = 0.005f }
        }
    },
    [SAVANNA] = {
        .top_block = GRASS,
        .bottom_block = DIRT,
        .roughness = 1.0f,
        .scale = 1.0f,
        .exp = 1.0f,
        .decorations = {
            { .f = worldgen_tree, .chance = 0.001f },
            { .f = worldgen_flowers, .chance = 0.001f },
            { .f = worldgen_grass, .chance = 0.005f },
        }
    },
    [JUNGLE] = {
        .top_block = GRASS,
        .bottom_block = DIRT,
        .roughness = 1.0f,
        .scale = 1.0f,
        .exp = 1.0f,
        .decorations = {
            { .f = worldgen_tree, .chance = 0.01f },
            { .f = worldgen_flowers, .chance = 0.001f },
            { .f = worldgen_grass, .chance = 0.01f },
        }
    },
    [GRASSLAND] = {
        .top_block = GRASS,
        .bottom_block = DIRT,
        .roughness = 1.0f,
        .scale = 1.0f,
        .exp = 1.0f,
        .decorations = {
            { .f = worldgen_tree, .chance = 0.0005f },
            { .f = worldgen_flowers, .chance = 0.003f },
            { .f = worldgen_grass, .chance = 0.02f },
        }
    },
    [WOODLAND] = {
        .top_block = GRASS,
        .bottom_block = DIRT,
        .roughness = 1.0f,
        .scale = 1.0f,
        .exp = 1.0f,
        .decorations = {
            { .f = worldgen_tree, .chance = 0.007f },
            { .f = worldgen_flowers, .chance = 0.003f },
            { .f = worldgen_grass, .chance = 0.008f },
        }
    },
    [FOREST] = {
        .top_block = GRASS,
        .bottom_block = DIRT,
        .roughness = 1.0f,
        .scale = 1.0f,
        .exp = 1.0f,
        .decorations = {
            { .f = worldgen_tree, .chance = 0.009f },
            { .f = worldgen_flowers, .chance = 0.003f },
            { .f = worldgen_grass, .chance = 0.008f },
        }
    },
    [RAINFOREST] = {
        .top_block = GRASS,
        .bottom_block = DIRT,
        .roughness = 1.0f,
        .scale = 1.0f,
        .exp = 1.0f,
        .decorations = {
            { .f = worldgen_tree, .chance = 0.009f },
            { .f = worldgen_flowers, .chance = 0.003f },
            { .f = worldgen_grass, .chance = 0.008f },
        }
    },
    [TAIGA] = {
        .top_block = PODZOL,
        .bottom_block = DIRT,
        .roughness = 1.0f,
        .scale = 1.0f,
        .exp = 1.0f,
        .decorations = {
            { .f = worldgen_pine, .chance = 0.006f },
            { .f = worldgen_flowers, .chance = 0.001f },
            { .f = worldgen_grass, .chance = 0.008f },
        }
    },
    [TUNDRA] = {
        .top_block = SNOW,
        .bottom_block = STONE,
        .roughness = 1.0f,
        .scale = 1.0f,
        .exp = 1.0f,
        .decorations = {
            { .f = worldgen_pine, .chance = 0.0005f }
        }
    },
    [ICE] = {
        .top_block = SNOW,
        .bottom_block = STONE,
        .roughness = 1.0f,
        .scale = 1.0f,
        .exp = 1.0f
    },
    [MOUNTAIN] = {
        .top_block = SNOW,
        .bottom_block = STONE,
        .roughness = 2.0f,
        .scale = 1.2f,
        .exp = 1.0f
    },
};

const enum Biome BIOME_TABLE[6][6] = {
    { ICE, TUNDRA, GRASSLAND,   DESERT,     DESERT,     DESERT },
    { ICE, TUNDRA, GRASSLAND,   GRASSLAND,  DESERT,     DESERT },
    { ICE, TUNDRA, WOODLAND,    WOODLAND,   SAVANNA,    SAVANNA },
    { ICE, TUNDRA, TAIGA,       WOODLAND,   SAVANNA,    SAVANNA },
    { ICE, TUNDRA, TAIGA,       FOREST,     JUNGLE,     JUNGLE },
    { ICE, TUNDRA, TAIGA,       TAIGA,      JUNGLE,     JUNGLE }
};

const f32 HEAT_MAP[] = {
    0.05f,
    0.18f,
    0.4f,
    0.6f,
    0.8f
};

const f32 MOISTURE_MAP[] = {
    0.2f,
    0.3f,
    0.5f,
    0.6f,
    0.7f
};

// h = height, [-1, 1]  m = moisture [0,1]  t = temperature [0,1]
// n = mountain noise [0,1]  i = modified heightmap [0,1]
typedef struct {
    f32 height;     // h
    f32 moisture;   // m
    f32 temperature;// t
    f32 mountain;   // n
    f32 peak;       // i
} BiomeQuery;

static size_t _lookup_heat(f32 t) {
    for (size_t i = 0; i < 4; i++) {
        if (t <= HEAT_MAP[i]) return i;
    }
    return 4;
}

static size_t _lookup_moisture(f32 m) {
    for (size_t i = 0; i < 4; i++) {
        if (m <= MOISTURE_MAP[i]) return i;
    }
    return 4;
}

static enum Biome get_biome(BiomeQuery q) {
    if (q.height <= 0.0f || q.mountain <= 0.0f) {
        return OCEAN;
    } else if (q.height <= 0.005f) {
        return BEACH;
    }

    if (q.mountain >= 0.1f && q.peak >= 0.2f) {
        return MOUNTAIN;
    }

    size_t t_i = _lookup_heat(q.temperature);
    size_t m_i = _lookup_moisture(q.moisture);

    return BIOME_TABLE[m_i][t_i];
}

static enum BlockId _get(struct Chunk *chunk, s32 x, s32 y, s32 z) {
    ivec3s p = (ivec3s) {{ x, y, z }};
    if (chunk_in_bounds(p)) {
        return chunk_get_block(chunk, p);
    } else {
        return world_get_block(chunk->world, glms_ivec3_add(chunk->position, p));
    }
}

static void _set(struct Chunk *chunk, s32 x, s32 y, s32 z, u32 d) {
    ivec3s p = (ivec3s){{x, y, z}};
    if (chunk_in_bounds(p)) {
        chunk_set_block(chunk, p, d);
    }
    
    ivec3s p_w = glms_ivec3_add(chunk->position, p);
    if (!world_contains(chunk->world, p_w)) {
        world_append_unloaded_block(chunk->world, p_w, d);
    } else {
        world_set_block(chunk->world, p_w, d);
    }
}


/* Apply registered decorations to a surface position. */
static void _run_decorations(struct Chunk *chunk,
                             const struct Decoration decorations[],
                             FWGGet get, FWGSet set, s64 x, s64 y, s64 z)
{
    for (size_t i = 0; i < MAX_DECORATIONS; i++) {
        if (decorations[i].f == NULL) {
            break;
        }
        if (RANDCHANCE(decorations[i].chance)) {
            decorations[i].f(chunk, get, set, x, y, z);
        }
    }
}

/* Place the appropriate block at (x,y,z) based on world Y and terrain
 * height.  Returns false to signal "skip this layer". */
static bool _place_block(struct Chunk *chunk, s32 x, s32 y, s32 z,
                         f32 y_w, f32 h,
                         enum BlockId top_block, enum BlockId under_block)
{
    if (y_w > h && y_w <= WATER_LEVEL) {
        chunk_set_block(chunk, (ivec3s){{x, y, z}}, WATER);
        return true;
    }
    if (y_w > h) {
        return false;
    }
    if (y_w == h) {
        chunk_set_block(chunk, (ivec3s){{x, y, z}}, top_block);
        return true;
    }
    if (y_w >= h - 3) {
        chunk_set_block(chunk, (ivec3s){{x, y, z}}, under_block);
        return true;
    }
    chunk_set_block(chunk, (ivec3s){{x, y, z}}, STONE);
    return true;
}

/* Compute noise values for a single (x,z) column and store WorldgenData. */
static void _compute_column(struct Heightmap *heightmap, s64 x, s64 z,
                            s64 wx, s64 wz, u64 seed,
                            const struct Noise *n_h, const struct Noise *n_m,
                            const struct Noise *n_t, const struct Noise *n_r,
                            const struct Noise *n_n, const struct Noise *n_p)
{
    f32 h = n_h->compute(&n_h->params, seed, wx, wz),
        m = n_m->compute(&n_m->params, seed, wx, wz) * 0.5f + 0.5f,
        t = n_t->compute(&n_t->params, seed, wx, wz) * 0.5f + 0.5f,
        r = n_r->compute(&n_r->params, seed, wx, wz),
        n = n_n->compute(&n_n->params, seed, wx, wz),
        p = n_p->compute(&n_p->params, seed, wx, wz);

    // add 'peak' noise to mountain noise
    n += safe_expf(p, (1.0f - n) * 3.0f);

    // decrease temperature with height
    t -= 0.4f * n;
    t = clamp(t, 0.0f, 1.0f);

    enum Biome biome_id = get_biome((BiomeQuery){ h, m, t, n, n + h });
    struct BiomeData biome = BIOME_DATA[biome_id];

    h = sign(h) * fabsf(powf(fabsf(h), biome.exp));

    heightmap->worldgen_data[x * CHUNK_SIZE.x + z] = (struct WorldgenData) {
        .h_b = ((h * 32.0f) + (n * 256.0f)) * biome.scale + (biome.roughness * r * 2.0f),
        .b = biome_id
    };
}

void worldgen_generate(struct Chunk *chunk) {
    SRAND(chunk->world->seed + ivec3shash(chunk->offset));

    struct Heightmap *heightmap = chunk_get_heightmap(chunk);

    // generate worldgen data if it doesn't exist for this chunk column yet
    if (!heightmap->flags.generated) {
        heightmap->flags.generated = true;

        struct Noise bs[] = {
            basic(1), basic(2),
            basic(3), basic(4)
        };

        struct Noise os[] = {
            octave(5, 0), octave(5, 1),
            octave(5, 2), octave(5, 3),
            octave(5, 4), octave(5, 5),
        };

        struct Noise cs[] = {
            combined(&bs[0], &bs[1]),
            combined(&bs[2], &bs[3]),
            combined(&os[3], &os[4]),
            combined(&os[1], &os[2]),
            combined(&os[1], &os[3])
        };

        struct Noise
            n_h = expscale(&os[0], 1.3f, 1.0f / 128.0f),
            n_m = expscale(&cs[0], 1.0f, 1.0f / 512.0f),
            n_t = expscale(&cs[1], 1.0f, 1.0f / 512.0f),
            n_r = expscale(&cs[2], 1.0f, 1.0f / 16.0f),
            n_n = expscale(&cs[3], 3.0f, 1.0f / 512.0f),
            n_p = expscale(&cs[4], 3.0f, 1.0f / 512.0f);

        for (s64 x = 0; x < CHUNK_SIZE.x; x++) {
            for (s64 z = 0; z < CHUNK_SIZE.z; z++) {
                s64 wx = chunk->position.x + x, wz = chunk->position.z + z;

                _compute_column(heightmap, x, z, wx, wz, chunk->world->seed,
                                &n_h, &n_m, &n_t, &n_r, &n_n, &n_p);
            }
        }

#define WG_GET_H(_x, _z)\
    heightmap->worldgen_data[\
        clamp((_x), 0, CHUNK_SIZE.x - 1) * CHUNK_SIZE.x +\
        clamp((_z), 0, CHUNK_SIZE.z - 1)]

        // smooth heightmap
        for (s64 x = 0; x < CHUNK_SIZE.x; x++) {
            for (s64 z = 0; z < CHUNK_SIZE.z; z++) {
                f32 v = 0.0f;
                v += (WG_GET_H(x - 1, z - 1)).h_b;
                v += (WG_GET_H(x + 1, z - 1)).h_b;
                v += (WG_GET_H(x - 1, z + 1)).h_b;
                v += (WG_GET_H(x + 1, z + 1)).h_b;
                v *= 0.25f;
                WG_GET_H(x, z).h = v;
            }
        }
    }

    for (s64 x = 0; x < CHUNK_SIZE.x; x++) {
        for (s64 z = 0; z < CHUNK_SIZE.z; z++) {
            struct WorldgenData data = heightmap->worldgen_data[x * CHUNK_SIZE.x + z];
            s64 h = data.h;
            enum Biome biome = data.b;
            struct BiomeData biome_data = BIOME_DATA[biome]; 

            enum BlockId top_block = h > 48 ? SNOW : biome_data.top_block,
                under_block = biome_data.bottom_block;

            for (s64 y = 0; y < CHUNK_SIZE.y; y++) {
                s64 y_w = chunk->position.y + y;

                if (!_place_block(chunk, x, y, z, y_w, h,
                                  top_block, under_block)) {
                    continue;
                }

                if (y_w == h) {
                    _run_decorations(chunk, biome_data.decorations, _get, _set, x, y, z);
                }
            }
        }
    }
}