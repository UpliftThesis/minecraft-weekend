# Codebase structure report

This repository contains a C voxel game. `src/main.c` initializes the block registry, rendering, world, user interface and player, then runs the window and game loop. The game is intended to be run from the repository root so that relative asset paths resolve.

## Source layout

- `src/world/`: chunk storage and meshing, world loading, block and light access, heightmaps, sky and day/night state. `src/world/gen/` generates terrain and vegetation using noise. Chunks hold 32 × 32 × 32 cells, including block IDs and packed lighting and metadata.
- `src/block/`: block definitions and registration, including terrain, plants, liquids and light sources. Block properties and geometry influence rendering and gameplay.
- `src/entity/`: entity/component management, player behavior, movement, physics, controls, camera, block targeting and lighting.
- `src/gfx/`: GLFW window management, OpenGL rendering, shaders, textures, buffers and block atlas.
- `src/ui/`: crosshair and hotbar rendering and interaction.
- `src/util/`: math, vectors, directions, colors, ray traversal, bounding boxes, bitmaps and sorting helpers.
- `res/`: images and shaders loaded by the game; `screenshots/` contains a screenshot.
- `lib/`: vendored GLAD, stb image loader and noise implementation; cglm and GLFW are Git submodules.

## Build and test situation

The root `Makefile` builds the C11 program with Clang and static GLAD, cglm, GLFW and noise libraries. Its normal targets build dependencies and object files in their existing source directories. The README describes `make` and running `./bin/game` from the root. Graphics execution needs an appropriate windowing/OpenGL environment.

At the time of inspection, `lib/cglm` and `lib/glfw` are uninitialized submodules, so the normal build is unavailable without obtaining those dependencies. No existing test suite or test target was found. The current work must not modify existing files, so tests should use their own runner and build outputs under a newly created test directory rather than running the in-place project build.

## Regression testing seams

Dependency-light candidates include bitmaps, sorting and the vendored noise implementation. Direction conversion, voxel ray traversal, chunk indexing and packed light/data access are useful headless seams but much of the game includes cglm headers. ECS and world behavior could be covered with focused test doubles where dependencies permit. Graphics, asset loading, live interaction, world generation and lighting integration require additional dependencies or infrastructure; headless tests alone cannot establish that these work end to end.

Prefer assertions about inputs and observable results, including edge cases and stable mathematical properties, rather than source layout, private calls, or incidental implementation details. Suspected existing defects should be documented or investigated rather than automatically cemented as required behavior.
