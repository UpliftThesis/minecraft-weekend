# Extended regression suite

This suite exists to keep the game's behaviour fixed while the code is refactored for better code health. It complements the original headless checks in `tests/run.sh` (bitmap/sort, noise, ray casting, coordinate helpers, and a toy ECS). Instead of testing isolated helpers, it links **the whole game except `src/main.c`** and drives it headlessly through its public API.

| | |
|---|---|
| Test files | 14 (`tests/extended/test_*.c`), about 4,400 lines |
| Tests | 189: **174 pass**, **15 are known-defect tests (XFAIL)**. Two consecutive full runs gave identical results |
| Runtime | about 25 s for both suites, including the build. With `SANITIZE=1` it is about 1 min |
| Line coverage of `src/` | **77.3 %** (1,875 / 2,427 lines, gcov, excluding `main.c`). The original suite covers roughly 10 % |
| Mutation probe | 22 injected bugs. The extended suite alone catches 16 (after 3 gap-closing tests were added); together with `tests/run.sh` it catches 18. Of the 4 left: 2 cannot change behaviour, 1 is a deliberately unspecified ray-casting edge case, and 1 is not observably different. Details below |

## Running

```sh
./tests/run_all.sh                     # original suite + extended suite
./tests/extended/run.sh                # extended suite only
./tests/extended/run.sh torch          # only tests whose name contains "torch"
TESTS="test_light test_sky" ./tests/extended/run.sh   # only these files
SANITIZE=1 ./tests/extended/run.sh     # AddressSanitizer + UBSan build
./tests/extended/coverage.sh           # per-file line coverage table (gcc/gcov)
./tests/extended/mutation_probe.sh     # inject bugs into a copy of src/ and count detections
```

Requirements: a GNU-C compiler (`CC`, default `cc`; tested with gcc 15) and a POSIX shell. There is no network access, GPU, display, or initialised submodule needed. All build output goes to `tests/extended/build/`, which is git-ignored. The suite never writes outside `tests/`.

Exit status: non-zero if any test FAILs, or if any known-defect test unexpectedly passes (XPASS, see below).

## How it works

```
src/**/*.c (unmodified, except main.c)  ─┐
lib/glad, lib/noise, lib/stb (vendored)  ├─► build/<config>/libgame_headless.a ─┐
third_party/cglm (real headers)          │                                     ├─► one binary per test_*.c
support/fake_platform.c, harness.c      ─┘                    test_*.c ────────┘
```

* **Real production code.** Every file under `src/` except `main.c` (which owns the window loop) is compiled as-is.
* **Real cglm.** `lib/cglm` is an uninitialised submodule, so `third_party/cglm/` holds the cglm headers at exactly the commit the submodule pins (`c783c42`, MIT licence included). The original suite substitutes a hand-written cglm stand-in; this suite does not.
* **Fake platform** (`support/fake_platform.c`, `support/GLFW/glfw3.h`). GLFW functions are no-ops. All OpenGL entry points the game uses (through glad's function pointers) are replaced by recording fakes. These record only API-level observations (`fake_gl`: draw calls, indices drawn, bytes uploaded per buffer target, and the number of GL objects still alive), so meshing can be tested at the GL boundary rather than through internals. The file also defines the global `state` that normally lives in `main.c`.
* **Harness** (`support/harness.{h,c}`). This builds small deterministic worlds: `tw_create`, then fill with empty chunks (`tw_fill_empty`), chunks with chosen blocks placed the way the terrain generator places them (`tw_load_prefilled`), or chunks from the real terrain generator (`tw_load_generated`, `tw_fill_generated`). **All knowledge of how a `World` is assembled lives in this one file**, so a refactoring that changes world construction should only need edits here, not in the tests.
* **Framework** (`support/check.h`). Tests are declared with `TEST(name)`. Each test runs in a forked child, so a crash or a failing production `assert()` only fails that test. Checks: `CHECK`, `CHECK_EQ`, `CHECK_NEAR`, `CHECK_IVEC3_EQ`, `CHECK_MSG`, plus a seeded PRNG for property-style tests.

### Known defects (XFAIL / XPASS)

`KNOWN_DEFECT(name, "reason") { ... }` declares a test that asserts the **correct** behaviour for a bug that exists today. It fails now and is reported as `XFAIL`, with the reason and the failing check, and the suite stays green. If the defect disappears, for example because a refactoring fixes it intentionally or by accident, the test reports **`XPASS` and the suite fails**. The person who changed the behaviour must then convert it into an ordinary `TEST`, so the fix is recorded rather than silent.

This mechanism was verified. I applied the four one-line fixes for defects 1–4 below to a copy of `src/` (`SRC_ROOT=<copy> ./tests/extended/run.sh`). Each fix flipped exactly its own tests to XPASS and nothing else changed.

### Design rules used to avoid locking in the implementation

* **Behaviour through public functions.** Tests use `world_get/set_*`, `chunk_prepare/render`, `ecs_*`, `physics_move`, `sky_render`, `ray_block` and similar, not private fields. The few places that touch internals are listed under limitations.
* **Oracles instead of snapshots.**
  * Lighting: expected torchlight is computed by a shortest-path model written in the test. After an edit, the result is also compared with a freshly built world containing the same blocks.
  * Meshing: counts are expressed in "faces", where the indices per face are measured in-test, and checked against a face-visibility oracle. No vertex format or byte count is hard-coded.
  * Ray casting: checked against an analytic slab-intersection oracle.
  * Terrain: checked with invariants (deep underground is solid, high sky is air, the surface is a sensible block type, sea level holds) and with comparisons between worlds (same seed gives the same world, generation order does not matter). There are no golden block layouts.
  * Sky: periodicity, continuity, day brighter than night, and values within range. No exact colours.
* **Properties over examples.** Many tests apply hundreds of seeded random edits and check invariants after each batch.

## What is covered

| File | Production code | What is protected |
|---|---|---|
| `test_storage.c` (12) | `chunk.h`, `world.h` | Packed per-block storage: block, sunlight, torchlight, combined light and metadata all read back what was written; writes to one cell do not affect other cells; unloaded positions read as empty and ignore writes. Coordinate mapping (property-based): block position ↔ chunk offset + local position, float position → containing block, chunk-slot index bijection, and `contains` matching the loaded region. |
| `test_light.c` (25 + 1 XFAIL) | `light.c`, `chunk.c` | Torchlight falls off by Manhattan distance per channel; glass and leaves let light through; opaque blocks block and reroute it (sealed boxes, walls with holes); removal restores darkness; removing one of two torches equals a fresh world with the remaining torch; placement order does not matter; light crosses chunk borders and negative coordinates; 300 random edits match the oracle. Sunlight: open sky is full; opaque blocks shade the cells below, with sideways spill; roofs and openings fall off by distance; sealed boxes are dark whatever order they are built in; incremental edits equal a freshly loaded chunk. |
| `test_heightmap.c` (12) | `world.c` heightmap | Unknown for empty columns; raised only by solid, non-transparent blocks; removing the top block falls back to the next solid block (across chunks), then to unknown; negative coordinates and chunk edges; values computed at chunk load match the blocks; load order of stacked chunks does not matter; falling back reaches the lowest loaded layer; 1,500 random edits. |
| `test_world_loading.c` (16 + 2 XFAIL) | `world.c` | Re-centring keeps in-range chunks and their content, drops the rest, and moves heightmaps with their columns; re-centring within the same chunk does nothing; blocks queued for unloaded chunks are applied on load and keep their order; queue growth; `world_get_aabbs` returns exactly the solid blocks, as unit cubes; which blocks are solid. |
| `test_worldgen.c` (13) | `world/gen/*` | Determinism per seed; independence of load order, world layout and re-centring; different seeds and locations give different terrain; terrain invariants for several seeds and a far-away region; decorations exist; trees stand on ground. |
| `test_physics.c` (25 + 1 XFAIL) | `c_physics.c`, `c_position.c`, `c_movement.c`, `aabb.h` | Falling and resting on floors, including across chunk borders; free fall accelerates; walls stop movement in both directions with sliding; water slows falling; non-solid blocks are passable; never ending up inside solid blocks (12 random entities for 400 ticks); walking, jumping (not in mid-air; clears a 1-block ledge but not a 2-block wall; airtime of about half a second; climbing a step), flying, swimming; position and chunk change tracking; AABB helpers. |
| `test_ecs_game.c` (10 + 2 XFAIL) | `ecs.c` with the real component set | Entity ids are never reused while slots are; delete removes all components; events reach every component type of every entity; production components (position, physics, light, camera) are driven by events; growth past 64, 128 and 256 entities keeps data; component storage is aligned. |
| `test_meshing.c` (20) | `chunkmesh.c`, `blockmesh.c`, `chunk.c` | Empty chunk draws nothing; a cube draws 6 equal faces; shared faces are culled (pairs, 2×2×2, enclosed blocks, across chunk borders); transparent blocks go to the transparent pass; same-type transparent neighbours cull and different types do not; sprites and torches; editing re-meshes and an unchanged chunk does not; neighbour chunks are marked dirty on border edits; generated terrain matches a face-visibility oracle; `world_render` draws the chunk geometry on top of the sky. |
| `test_sky.c` (6) | `sky.c` | The day/night cycle is periodic; day is brighter than night; colours are in range and never NaN; changes from tick to tick are continuous; sunset and sunrise fade monotonically; the clear colour equals the fog colour. |
| `test_color_camera.c` (13 + 2 XFAIL) | `color.c`, `camera.c` | Lab round trip is the identity over an RGB grid; brighten and lerp behave correctly; perspective and orthographic matrices are well formed; projection puts points in front of the camera inside clip space and points behind outside; yaw and pitch, including the pitch clamp and yaw wrap. |
| `test_blocks.c` (10 + 1 XFAIL) | `block/*.c`, `blockatlas.c` | Every id is registered with its own id; air, liquids, emitters and sprite flags are consistent; physics modifiers; the AABB of a full cube; texture and animation coordinates stay inside the real atlas; custom meshes fit inside their cell. |
| `test_util.c` (9) | `fmath`, `direction.c`, `aabb.h`, `util.h` | `ivec3scmp`/`ivec3shash` semantics (no collisions on a 17³ grid); math macros, including that arguments are evaluated once; directions are unit vectors with unique opposites; `memsetl`; ray traversal against an analytic oracle (3,000 random rays). |
| `test_known_defects.c` (1 + 6 XFAIL) | various | The torchlight/sunlight defect and the other defects found while setting up the suite (see below). |
| `test_smoke.c` (2) | harness | Worlds can be built, filled and destroyed. |

### Coverage by file (gcov, `./tests/extended/coverage.sh`)

* **100 % or nearly:** `ecs.c`, `c_physics.c`, `c_position.c`, `c_camera.c`, `fmath.c`, `color.c`, `camera.c`, `blockmesh.c`, `worldgen.c`, `noise.c`, `flowergen.c`, `grassgen.c`, `light.c` (99 %), `sky.c` (99 %), `treegen.c` (96 %), and every `block/*.c` file.
* **High:** `c_movement.c` 92 %, `chunkmesh.c` 91 %, `chunk.c` 87 %, `c_light.c` 85 %, `world.c` 83 %, `blockatlas.c` 80 %.
* **Low or zero:**
  * Window and input: `window.c` 0 %, `c_control.c` 7 %, `c_blocklook.c` 12 %.
  * UI: `hotbar.c`, `crosshair.c`, `ui.c`, all 0 %.
  * Renderer and shaders: `renderer.c` 17 %, `shader.c` 33 %.
  * `shrubgen.c` 0 %: no biome that places shrubs occurred in the sampled worlds.

### Mutation probe (`./tests/extended/mutation_probe.sh`)

Each mutant is applied to a copy of `src/` under `build/mutation/`. The repository's own `src/` is never touched.

* **Caught (16):**
  * `ecs_event` skips the last component type
  * `ecs_delete` never frees the slot
  * `ivec3shash` ignores y and z
  * wrong y in `world_chunk_offset`
  * metadata field shifted
  * sunlight dims when travelling down
  * heightmap recalculation skips the lowest layer *(gap closed by `heightmap_fallback_reaches_lowest_loaded_layer`)*
  * `get_aabbs` uses transparency instead of solidity
  * pending-block queue removal corrupts the order
  * same-type transparent faces not culled
  * opaque faces emitted wrongly
  * gravity halved *(gap closed by `jump_clears_one_block_but_not_two`)*
  * `block_changed` never set
  * wrong sRGB gamma
  * day length off by one tick
  * field of view changed
* **Caught by the original suite only (2):** `bitmap_clr` toggles instead of clearing, and noise octave frequency ×3. The extended suite deliberately has no golden noise values; `tests/noise` pins those.
* **Not caught (4):**
  * **Two cannot change behaviour.**
    * `ecs_delete` keeping the id-map entry: the map is only read for occupied slots.
    * `torchlight_add` ignoring an opaque target: no light source is opaque.
  * **Ray traversal y/z tie-break:** grid-edge ties are deliberately left unspecified, as in `tests/math`.
  * **Decoration RNG seeded without the chunk position:** measured per-chunk decoration layouts are equally dissimilar with and without it, because biome and surface dominate. No robust test was found, so this is left uncovered.

## Known defects recorded as XFAIL (15 tests, 12 defects)

| # | Test(s) | Defect | Effect in the game |
|---|---|---|---|
| 1 | `torchlight_write_preserves_sunlight`, `torchlight_write_preserves_metadata`, `generated_torch_under_open_sky_keeps_sunlight` | `TORCHLIGHT_MASK` (`chunk.h`) is the 32-bit literal `0x000000000FFFF0000`, so `~mask` also clears bits 32–63 | Writing torchlight erases sunlight and metadata in that cell. A torch present when its chunk is generated has sunlight 0 in its own cell. |
| 2 | `metadata_roundtrips_values_with_bit3_set` | `METADATA_MASK` is `0xFFFFFF7…`, so bit 39 is missing | Metadata values with bit 3 set do not round-trip. Metadata is currently unused. |
| 3 | `generated_torch_spreads_all_colour_channels` | `light_apply` reads `torchlight_queue.elements[i]` (the channel index) instead of `[j]` | A light source present at chunk generation only spreads its intensity channel, not R, G or B. |
| 4 | `editing_top_layer_remeshes_chunk_above` | `chunk_get_bordering_chunks` tests `pos.x` instead of `pos.y` for the +Y neighbour | Editing a chunk's top layer does not re-mesh the chunk above it. |
| 5 | `removing_torch_next_to_lava_keeps_lava_light` | `remove_propagate` clears a neighbouring light source's own cell and never restores it | Removing a light next to a different-coloured source (e.g. lava) permanently dims that source. |
| 6 | `lab_reference_values_for_white_and_grey`, `rgba_lerp_between_black_and_white_is_neutral_grey` | `rgb2xyz`/`xyz2rgb` fill row-major constants into cglm's column-major `mat3s`, so both matrices are transposed. Round trips hide it. | Lab values are wrong. Black↔white blends are tinted pink, which affects sunset and sunrise sunlight. |
| 7 | `animated_blocks_are_textured_with_their_first_animation_frame` | Lava's animation frames are `(14, i)` but its texture is `(0, 14)`: x and y are swapped (water is consistent) | Lava is never animated, and placeholder sprites are copied over. |
| 8 | `pending_blocks_consecutive_for_same_chunk_all_applied` | `world_load_chunk` removes queue entry `i` and then still does `i++` | Consecutive queued blocks for the same chunk are skipped, so parts of trees that spill into neighbouring chunks go missing. 1,128 stale entries were left after generating one test world. |
| 9 | `get_aabbs_respects_capacity` | In `world_get_aabbs`, the `break` at `i == n` only exits the innermost loop | Writes past the caller's buffer, a memory-safety bug: 61 boxes were written into a capacity of 5. |
| 10 | `flying_player_does_not_clip_into_walls` | `physics_move` gathers colliders only around the start box (scaled ×2), not along the path of the move | Fast movement, for example flying, clips about 0.15 blocks into walls before being pushed out. |
| 11 | `destroy_hook_runs_only_for_components_the_entity_has` | `ecs_delete` runs every component type's destroy hook, including for components the entity never had or that were already removed | Hooks run twice or for absent components. Latent today, because no production component has a destroy hook. |
| 12 | `component_added_without_value_starts_zeroed_in_reused_slot` | Delete and remove never clear component storage | A new entity in a reused slot inherits the previous occupant's data, e.g. position (7,8,9). |

Found but not encoded as tests, because they are not observable through the public API or the fake GL:

* **ECS tag misalignment.** `ecs.c` stores a `u64` tag immediately before each component, so it is misaligned whenever a component's size is not a multiple of 8. UBSan reports this in production component storage (`ecs.c:36/114/135/136/154/155/164`) under `SANITIZE=1`. It is undefined behaviour that happens to work on x86-64.
* **Block atlas animation copies only the red channel.** `blockatlas.c` `copy_pixels` copies byte 0 of each pixel only, so animation frames would update only the red channel.
* **Chunk block count is wrong.** `chunk_on_modify` increments `count` on any block change whose new block is not AIR, e.g. STONE→DIRT, so `count` drifts upward and a chunk may never be recognised as empty again. This only affects performance.
* **Author's own note.** The TODO in `light.c` about sunlight propagating down a column it entered sideways; no test asserts behaviour in that area.

## Limitations and what is still missing

* **Window, input and UI are not covered.** `window.c`, `c_control.c` (keyboard and mouse to movement), `c_blocklook.c` (block picking, placing and breaking via the mouse), the hotbar and crosshair. These need simulated input via `window.keyboard`/`window.mouse` (the fake GLFW makes this possible) and would be the next tests to add.
* **No pixel-level rendering checks.** Shaders, textures and the renderer only run against no-op GL. Visual regressions, such as wrong UVs or vertex light values, are not detected. A golden-image test would need a real GL context, for example with Mesa's software renderer.
* **`shrubgen.c` and some biome paths are not reached.**
* **The game binary itself is not built by the suite.** The Makefile build still needs the cglm and GLFW submodules. Building the real game should remain part of the refactoring check.
* **Some coupling to internals remains,** each documented in its file:
  * `harness.c`: world construction.
  * `test_meshing.c`: the per-frame meshing throttle, plus atlas and viewer setup.
  * `test_world_loading.c`: reads the pending-block queue (there is no public accessor for its order) and `chunks_origin`.
  * Component flags such as `grounded`, `flying` and `collide` in the physics and ECS tests.

  If these structures are renamed during refactoring, update the helpers rather than weakening the assertions.
* **XFAIL tests can mask a different failure.** An XFAIL test that fails for another reason, such as a crash, is still reported as XFAIL. The runner prints the failing check under each XFAIL, so check the output when the count of known defects changes.
* **Tested with gcc only.** No clang is installed in this environment; the code uses GNU C extensions that clang also supports.
* **Sanitizer build.** It reports the ECS misalignment above but does not fail on it (UBSan runs in recover mode).

## Files

```
tests/run_all.sh                 runs tests/run.sh and tests/extended/run.sh
tests/extended/
  README.md                      this report
  run.sh                         builds the headless game library and runs all test_*.c
  coverage.sh                    gcov line coverage per src/ file
  mutation_probe.sh              mutation testing against a copy of src/
  support/check.h                test framework (TEST, KNOWN_DEFECT, CHECK*, PRNG)
  support/harness.{h,c}          world-building helpers (the single place coupled to world internals)
  support/fake_platform.{h,c}    recording fake OpenGL, no-op GLFW, global `state`
  support/GLFW/glfw3.h           minimal GLFW declarations
  third_party/cglm/              cglm headers at the pinned submodule commit (MIT)
  test_*.c                       the tests
```
