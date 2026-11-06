#!/bin/bash
# Mutation probe: injects small, realistic bugs into a COPY of src/ (under
# tests/extended/build/mutation) and checks whether the extended suite notices.
# The repository's own src/ is never modified.
#   tests/extended/mutation_probe.sh            # run all mutants
# Each mutant: file | perl substitution | description
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
work="$here/build/mutation"
mkdir -p "$work"

MUTANTS=$(cat <<'LIST'
src/util/bitmap.h|s/\&= ~\(1/^= (1/|bitmap_clr toggles instead of clearing
src/entity/ecs.c|s/for \(size_t i = 0; i <= ECSCOMPONENT_LAST; i\+\+\) \{\n        struct ComponentList \*list = &self->lists\[i\];\n        \n/for (size_t i = 0; i < ECSCOMPONENT_LAST; i++) {\n        struct ComponentList *list = &self->lists[i];\n/|ecs_event skips the last component type
src/entity/ecs.c|s/self->ids\[entity.index\] = ENTITY_NONE;//|ecs_delete keeps the id mapping
src/entity/ecs.c|s/bitmap_clr\(self->used, entity.index\);//|ecs_delete never frees the slot
src/util/fmath.c|s/if \(tmax.y < tmax.z\)/if (tmax.y <= tmax.z)/|ray traversal y/z tie-break
src/util/fmath.c|s/h \^= v.raw\[i\] \+ 0x9e3779b9/h ^= v.raw[0] + 0x9e3779b9/|ivec3shash ignores y and z
src/world/gen/noise.c|s/u \*= 2.0f/u *= 3.0f/|octave noise frequency x3
src/world/world.h|s/i % self->chunks_size,\n/(i \/ self->chunks_size) % self->chunks_size,\n/|world_chunk_offset computes wrong y
src/world/chunk.h|s/METADATA_OFFSET 36/METADATA_OFFSET 37/|metadata field shifted
src/world/light.c|s/u32 delta = sunlight_down \? 0 : -1;/u32 delta = -1;/|sunlight dims when travelling down
src/world/light.c|s/if \(\!BLOCKS\[world_get_block\(world, pos\)\].transparent\) \{\n        return;/if (0) {\n        return;/|torchlight_add ignores opaque target
src/world/world.c|s/for \(s64 y = y_max; y >= y_min; y--\)/for (s64 y = y_max; y > y_min; y--)/|heightmap recalculation skips lowest layer
src/world/world.c|s/if \(block.solid\) \{/if (!block.transparent) {/|get_aabbs uses transparency instead of solidity
src/world/world.c|s/self->unloaded_blocks.list \+ \(i \+ 1\),/self->unloaded_blocks.list + (i + 0),/|unloaded queue removal corrupts order
src/world/chunkmesh.c|s/params.block_n->id != params.block->id\)\)/1))/|same transparent neighbours not culled
src/world/chunkmesh.c|s/\(params.block_n->transparent && \!params.block->transparent\)/(params.block_n->transparent)/|opaque faces emitted into transparent neighbours wrongly
src/entity/c_physics.c|s/-0.0086f/-0.0043f/|gravity halved
src/entity/c_position.c|s/c_position->block_changed = true;/c_position->block_changed = false;/|block_changed never set
src/util/color.c|s/pow\( \( c.x \+ 0.055 \) \/ 1.055, 2.4 \)/pow( ( c.x + 0.055 ) \/ 1.055, 2.2 )/|sRGB gamma wrong for red
src/world/sky.c|s/TOTAL_DAY_TICKS/(TOTAL_DAY_TICKS + 1)/|day length off by one tick
src/world/gen/worldgen.c|s/SRAND\(chunk->world->seed \+ ivec3shash\(chunk->offset\)\)/SRAND(chunk->world->seed)/|decoration RNG ignores chunk position
src/util/camera.c|s/glms_perspective\(/glms_perspective(1.1f * /|camera field of view changed
LIST
)

killed=0; survived=0; total=0
while IFS='|' read -r file subst desc; do
    [ -n "$file" ] || continue
    total=$((total + 1))
    rm -rf "$work/src"; cp -r "$root/src" "$work/src"
    perl -0pi -e "$subst" "$work/$file"
    if cmp -s "$root/$file" "$work/$file"; then
        echo "NOT-APPLIED  $desc ($file)"; total=$((total - 1)); continue
    fi
    if SRC_ROOT="$work" BUILD_DIR="$work/build" sh "$here/run.sh" >"$work/last.log" 2>&1; then
        survived=$((survived + 1)); echo "SURVIVED     $desc ($file)"
    else
        killed=$((killed + 1))
        which=$(grep -E "^== test_|  FAIL |  XPASS" "$work/last.log" | awk '/^== /{t=$2} /FAIL|XPASS/{print t}' | sort -u | tr '\n' ' ')
        echo "KILLED       $desc ($file) by: $which"
    fi
done <<EOF2
$MUTANTS
EOF2
echo "mutation score: $killed / $total killed ($survived survived)"
