#include "worldgen.h"
#include "../world.h"

/* Check whether xx lies on the ring's x-boundary. */
static bool on_x_edge(s32 cx, s32 half_w, s32 xx) {
    return (xx == (cx - half_w)) || (xx == (cx + half_w));
}

/* Check whether zz lies on the ring's z-boundary. */
static bool on_z_edge(s32 cz, s32 half_w, s32 zz) {
    return (zz == (cz - half_w)) || (zz == (cz + half_w));
}

/* Check whether (x,z) sits exactly on the trunk column centre. */
static bool on_trunk_col(s32 x, s32 z, s32 cx, s32 cz) {
    return x == cx && z == cz;
}

/* Determine whether the trunk-centre exclusion applies at this height. */
static bool trunk_excluded(bool hollow_centre, bool on_trunk, bool above_base) {
    if (!hollow_centre) {
        return false;
    }
    if (!on_trunk) {
        return false;
    }
    if (above_base) {
        return false;
    }
    return true;
}

/* Determine whether random thinning should discard this corner cell. */
static bool corner_thinned(bool corner, bool top_layer, f32 randchance) {
    if (!corner) {
        return false;
    }
    if (!top_layer) {
        return false;
    }
    return RANDCHANCE(randchance);
}

/* Place a leaf at (xx,yy,zz) unless exclusion rules forbid it. */
static void place_leaf_cell(struct Chunk *chunk, FWGSet set,
                            s32 xx, s32 yy, s32 zz,
                            bool skip_trunk,
                            bool skip_corner) {
    if (skip_trunk) {
        return;
    }
    if (skip_corner) {
        return;
    }
    set(chunk, xx, yy, zz, LEAVES);
}

/* Iterate every height along a single (xx,zz) column of a tree ring. */
static void place_leaf_column(struct Chunk *chunk, FWGSet set,
                              s32 cx, s32 cz, s32 xx, s32 zz,
                              s32 half_w, s32 y_lo, s32 y_hi,
                              f32 randchance, bool hollow_centre) {
    bool ex = on_x_edge(cx, half_w, xx);
    bool ez = on_z_edge(cz, half_w, zz);
    bool corner = ex && ez;
    bool centered = on_trunk_col(xx, zz, cx, cz);

    for (s32 yy = y_lo; yy <= y_hi; yy++) {
        bool is_top = (yy == y_hi);
        bool above_base = (yy > y_lo);
        bool skip_trunk = trunk_excluded(hollow_centre, centered, above_base);
        bool skip_corner = corner_thinned(corner, is_top, randchance);

        place_leaf_cell(chunk, set, xx, yy, zz, skip_trunk, skip_corner);
    }
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
            place_leaf_column(chunk, set, cx, cz, xx, zz,
                              half_w, y_lo, y_hi, randchance, hollow_centre);
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

/* Check the radius-based exclusion used by pine cone placement.
   Note: compares against radius value 'r' instead of absolute coord —
   this replicates the original behaviour verbatim including the known
   defect. */
static bool excluded_by_radius(s32 x, s32 z, s32 r) {
    return x == r && z == r;
}

/* Set leaves for a single grid cell of a pine cone layer. */
static void place_pine_cell(struct Chunk *chunk, FWGSet set,
                            s32 px, s32 pz, s32 r,
                            s32 yy, s32 xx, s32 zz) {
    s32 c = 0;
    c += xx == (px - r) || xx == (px + r);
    c += zz == (pz - r) || zz == (pz + r);
    bool corner = c == 2;

    /* Corner cells and the radius-defect cell are never placed. */
    if (corner) {
        return;
    }
    if (excluded_by_radius(xx, zz, r)) {
        return;
    }
    set(chunk, xx, yy, zz, PINE_LEAVES);
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

/* Check whether this block type can support pine tree growth. */
static bool is_valid_pine_ground(enum BlockId under) {
    return (under == GRASS || under == DIRT || under == SNOW ||
            under == PODZOL);
}

void worldgen_pine(struct Chunk *chunk, FWGGet get, FWGSet set, s64 x, s64 y, s64 z) {
    enum BlockId under = get(chunk, x, y, z);
    if (!is_valid_pine_ground(under)) {
        return;
    }

    s64 h = RAND(5, 7);

    for (s32 yy = y + 1; yy <= (y + h); yy++) {
        set(chunk, x, yy, z, PINE_LOG);
    }

    build_pine_cone(chunk, set, x, y, z, h);

    set(chunk, x, y + h + 1, z, PINE_LEAVES);
}
