#include "worldgen.h"
#include "../world.h"

static inline bool is_on_edge(s32 pos, s32 centre, s32 range) {
    return pos == (centre - range) || pos == (centre + range);
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

    /* Lower canopy — 5×5 ring around trunk */
    for (s32 xx = (x - 2); xx <= (x + 2); xx++) {
        for (s32 zz = (z - 2); zz <= (z + 2); zz++) {
            for (s32 yy = (y + h); yy <= (y + h + 1); yy++) {
                if (!(is_on_edge(xx, x, 2))) continue;
                if (!(is_on_edge(zz, z, 2))) continue;

                bool corner = true;  /* both x and z are on edges */

                if ((!(xx == x && zz == z) || yy > (y + h)) &&
                    !(corner && yy == (y + h + 1) && RANDCHANCE(0.4))) {
                    set(chunk, xx, yy, zz, LEAVES);
                }
            }
        }
    }

    /* Upper canopy — 3×3 ring */
    for (s32 xx = (x - 1); xx <= (x + 1); xx++) {
        for (s32 zz = (z - 1); zz <= (z + 1); zz++) {
            for (s32 yy = (y + h + 2); yy <= (y + h + lh); yy++) {
                if (!(is_on_edge(xx, x, 1))) continue;
                if (!(is_on_edge(zz, z, 1))) continue;

                bool corner = true;  /* both x and z are on edges */

                if (!(corner && yy == (y + h + lh) && RANDCHANCE(0.8))) {
                    set(chunk, xx, yy, zz, LEAVES);
                }
            }
        }
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

    s64 r = RAND(2, 3);

    for(s64 yy = (y + 3); yy <= (y + h); yy++) {
        if (yy != (y + h) && yy % 2 == 0) {
            continue;
        }

        if (yy == (y + h)) {
            r = 1;
        } else if (r > 1 && RANDCHANCE(0.7)) {
            r--;
        }

        for (s32 xx = (x - r); xx <= (x + r); xx++) {
            for (s32 zz = (z - r); zz <= (z + r); zz++) {
                s32 c = 0;
                c += xx == (x - r) || xx == (x + r);
                c += zz == (z - r) || zz == (z + r);
                bool corner = c == 2;

                if (!corner && !(xx == r && zz == r)) {
                    set(chunk, xx, yy, zz, PINE_LEAVES);
                }
            }
        }
    }

    set(chunk, x, y + h + 1, z, PINE_LEAVES);
}