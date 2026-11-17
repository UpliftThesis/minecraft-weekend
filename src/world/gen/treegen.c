#include "worldgen.h"
#include "../world.h"

/* Decide whether to set a leaf for one tree column at height yy. */
static void place_leaf_cell(struct Chunk *chunk, FWGSet set,
                            s32 cx, s32 cz,
                            s32 half_w,
                            s32 xx, s32 zz, s32 yy,
                            bool hollow_centre, bool is_top,
                            f32 randchance) {
    s32 c = 0;
    c += xx == (cx - half_w) || xx == (cx + half_w);
    c += zz == (cz - half_w) || zz == (cz + half_w);
    bool corner = c == 2;

    /* Skip trunk centre on the lowest canopy layer. */
    if (hollow_centre && xx == cx && zz == cz && !is_top) {
        return;
    }
    /* Randomly skip some top-layer corners. */
    if (corner && is_top && RANDCHANCE(randchance)) {
        return;
    }
    set(chunk, xx, yy, zz, LEAVES);
}

/* Place a uniform leaf ring around a trunk column.

   Parameters differentiate lower canopy (radius 2, hollow centre at base,
   randchance 0.4) from upper canopy (radius 1, no centre hollow,
   randchance 0.8).  Every (xx,yy,zz) in the bounding box is visited
   exactly once, in the same order as the original unextracted loops. */
static void place_leaf_ring(struct Chunk *chunk, FWGSet set,
                            s32 cx, s32 cz,
                            s32 half_w, s32 y_lo, s32 y_hi,
                            f32 randchance, bool hollow_centre) {
    for (s32 xx = cx - half_w; xx <= cx + half_w; xx++) {
        for (s32 zz = cz - half_w; zz <= cz + half_w; zz++) {
            for (s32 yy = y_lo; yy <= y_hi; yy++) {
                bool is_top = (yy == y_hi);
                place_leaf_cell(chunk, set, cx, cz, half_w,
                                xx, zz, yy, hollow_centre, is_top, randchance);
            }
        }
    }
}

void worldgen_tree(struct Chunk *chunk, FWGGet get, FWGSet set, s64 x, s64 y, s64 z) {
    enum BlockId under = get(chunk, x, y, z);
    if (under != GRASS && under != DIRT) {
        return;
    }

    s32 h = RAND(3, 5);

    for (s32 yy = y + 1; yy <= (y + h); yy++) {
        set(chunk, x, yy, z, LOG);
    }

    s32 lh = RAND(2, 3);

    place_leaf_ring(chunk, set, (s32)x, (s32)z, 2,
                    (s32)y + h, (s32)y + h + 1, 0.4f, true);
    place_leaf_ring(chunk, set, (s32)x, (s32)z, 1,
                    (s32)y + h + 2, (s32)y + h + lh, 0.8f, false);
}

/* Set leaves for a single grid cell of a pine cone layer. */
static void place_pine_cell(struct Chunk *chunk, FWGSet set,
                            s32 px, s32 pz, s32 r,
                            s32 yy, s32 xx, s32 zz) {
    s32 c = 0;
    c += xx == (px - r) || xx == (px + r);
    c += zz == (pz - r) || zz == (pz + r);
    bool corner = c == 2;

    /* Note: xx == r / zz == r replicates original behaviour verbatim
       including the known defect that compares against radius, not coord. */
    if (!corner && !(xx == r && zz == r)) {
        set(chunk, xx, yy, zz, PINE_LEAVES);
    }
}

/* Walk every cell of a square grid centred on (px,pz) with side r. */
static void place_pine_grid(struct Chunk *chunk, FWGSet set,
                            s32 px, s32 pz, s32 r, s32 yy) {
    for (s32 xx = px - r; xx <= px + r; xx++) {
        for (s32 zz = pz - r; zz <= pz + r; zz++) {
            place_pine_cell(chunk, set, px, pz, r, yy, xx, zz);
        }
    }
}

static void build_pine_cone(struct Chunk *chunk, FWGSet set,
                            s64 x, s64 y, s64 z, s64 h) {
    s64 r = RAND(2, 3);

    for (s64 yy = y + 3; yy <= y + h; yy++) {
        if (yy != y + h && yy % 2 == 0) {
            continue;
        }

        if (yy == y + h) {
            r = 1;
        } else if (r > 1 && RANDCHANCE(0.7)) {
            r--;
        }

        s32 sr = (s32)r;
        place_pine_grid(chunk, set, (s32)x, (s32)z, sr, (s32)yy);
    }
}

void worldgen_pine(struct Chunk *chunk, FWGGet get, FWGSet set, s64 x, s64 y, s64 z) {
    enum BlockId under = get(chunk, x, y, z);
    if (under != GRASS && under != DIRT && under != SNOW && under != PODZOL) {
        return;
    }

    s64 h = RAND(5, 7);

    for (s32 yy = y + 1; yy <= (y + h); yy++) {
        set(chunk, x, yy, z, PINE_LOG);
    }

    build_pine_cone(chunk, set, x, y, z, h);

    set(chunk, x, y + h + 1, z, PINE_LEAVES);
}
