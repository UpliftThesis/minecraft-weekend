# Task: Establish a Clean, Reproducible Build Baseline

## Purpose

Create a known-good build baseline for `minecraft-weekend` before any gameplay,
architecture, performance, or cleanup refactoring begins. The baseline should
preserve the current game behavior while making the build reproducible on a
modern Apple Silicon Mac.

The baseline is a separate commit after the untouched upstream source. It is
not a feature commit.

## Reference State

Verify the fork's starting point before changing anything. The checkout used to
write this handoff had:

- Main repository commit: `387ecb554946180094d58c57a4ee216dba506cc4`
- CGLM submodule: `c783c4210152522ef61f40863f7c91f74186cb2a`
- GLFW submodule: `0b9e48fa3df9c184ff1abfb2452fd1a4b696ecd8`
- Target platform tested: Apple Silicon (`arm64`), macOS

If the fork starts at a different main commit, inspect its Makefile, submodule
pins, and API usage before applying these instructions. Do not blindly copy a
patch made for another revision.

## Known Problems

### 1. The build compiles an unused CGLM static library

The game uses CGLM's header-only `glm_` and `glms_` APIs. At the reference
commit, it has no calls to the compiled `glmc_` API:

```sh
rg '\bglmc_[A-Za-z0-9_]+' src
```

The original Makefile nevertheless builds and links `lib/cglm/libcglm.a`.
Modern Apple Clang rejects that old CGLM revision because its
`glmc_vec4_scale_as` declaration incorrectly uses `vec3` while the
implementation uses `vec4`. Upstream corrected the typo later, but this game
does not need the compiled library at all.

Do not make `-Wno-error=array-parameter` part of the baseline. That was only a
temporary diagnostic workaround used to prove the original game could build.

Recommended resolution:

- Keep the CGLM submodule because the game needs its headers.
- Stop building and linking `libcglm.a` if the source still has no `glmc_`
  calls.
- If the fork has gained `glmc_` calls, do not remove the library; update or
  patch CGLM deliberately instead and document that decision.

### 2. The pinned GLFW predates proper Apple Silicon monitor handling

The reference GLFW revision is from December 2020. On Apple Silicon it prints:

```text
GLFW error 65544: Cocoa: Failed to find service port for display
```

The warning is non-fatal, but it comes from an obsolete IOKit monitor-name
path. Upgrade the GLFW submodule to the conservative `3.3.10` tag:

```text
dc46d3f8129712e42856c20e99a604a3b08ad581
```

This keeps the project in GLFW's 3.3 API line while including the Apple Silicon
monitor fix. Do not combine a larger GLFW 3.5.x migration with this baseline
unless 3.3.10 cannot build; if a larger upgrade is necessary, explain why in
the commit message.

### 3. Dependency builds happen inside submodule source directories

The original Makefile runs CMake inside `lib/cglm` and `lib/glfw`. This leaves
generated files in the submodules and makes the repository appear dirty after
a build.

Use out-of-source build directories under an ignored top-level directory such
as `build/`. No generated file should be written into either submodule.

### 4. The Make dependency graph is not incremental

The original `libs` recipe runs on every `make`, and the target named `game`
produces `bin/game` instead of a file named `game`. A second `make` therefore
reconfigures or relinks work unnecessarily.

Give generated libraries, objects, and `bin/game` real file targets with
correct prerequisites. A second `make` with no source changes should report
that there is nothing to do.

### 5. GLFW builds unnecessary projects

Configure only the static GLFW library. Disable examples, tests,
documentation, and installation output:

```text
-DGLFW_BUILD_EXAMPLES=OFF
-DGLFW_BUILD_TESTS=OFF
-DGLFW_BUILD_DOCS=OFF
-DGLFW_INSTALL=OFF
```

## Required Scope

The baseline commit should:

1. Verify and initialize the recorded submodules.
2. Pin GLFW to `3.3.10`.
3. Treat CGLM as header-only if the game still has no compiled CGLM calls.
4. Build all generated files outside the dependency source directories.
5. Build only the required GLFW static library.
6. Give `bin/game` and its libraries/objects correct Make prerequisites.
7. Preserve `make`, `make run`, and `make clean` as clear entry points.
8. Add only the necessary generated directories to `.gitignore`.
9. Document the required tools (`clang`, `make`, and `cmake`) without vendoring
   a CMake executable into the repository.
10. Keep game source and behavior unchanged.

It is acceptable to compile GLAD and libnoise directly into the ignored build
directory instead of invoking their in-source Makefiles, provided their source
and compile flags remain equivalent.

## Explicitly Out of Scope

Do not include any of the following in the baseline commit:

- Mouse-wheel hotbar selection
- Inventory, pause, or settings menus
- Changes to controls or gameplay
- New blocks, rendering changes, or world-generation changes
- Broad warning suppression
- General C modernization or prototype cleanup
- Performance refactoring
- A large dependency modernization beyond what is required for the clean build

Those should be separate commits after the baseline so regressions are easy to
isolate.

## Acceptance Criteria

Starting from a clean clone with the intended baseline changes committed:

1. Submodules initialize successfully:

   ```sh
   git submodule update --init --recursive
   git submodule status
   ```

2. A clean build succeeds on Apple Silicon:

   ```sh
   make clean
   make
   ```

3. A second build performs no compilation, configuration, or linking:

   ```sh
   make
   ```

4. The output is a native executable:

   ```sh
   file bin/game
   otool -L bin/game
   ```

5. The game must be launched from the repository root so relative resource
   paths resolve:

   ```sh
   ./bin/game
   ```

6. The window opens and renders the world, keyboard and mouse input work, and
   the old `Failed to find service port for display` warnings are absent.

7. A build leaves both submodules clean. Generated files may be ignored at the
   top level, but must not make a submodule dirty:

   ```sh
   git status --short --ignore-submodules=none
   git -C lib/cglm status --short
   git -C lib/glfw status --short
   ```

8. No `glmc_` symbols are required by the game, and `libcglm.a` is neither
   built nor linked when CGLM is being used header-only.

9. The tracked diff is limited to build documentation/configuration and the
   intended GLFW submodule pointer change. There should be no gameplay-source
   changes.

## Smoke-Test Controls

Use these only to verify that the baseline preserved existing behavior:

- `W`, `A`, `S`, `D`: move
- Mouse: look
- `Space`: jump or ascend
- Left Shift: descend
- `K`: toggle flight
- Left click: remove a block
- Right click: place the selected block
- `1` through `9`, then `0`: select the ten hotbar slots
- `Esc`: release or recapture the mouse
- `T`: toggle wireframe rendering

There is intentionally no menu and no mouse-wheel hotbar selection in this
baseline.

## Suggested Commit Boundary

Create one focused commit after the untouched upstream reference, for example:

```text
build: establish reproducible Apple Silicon baseline
```

Before handing the repository to refactoring agents, record or tag both:

```text
upstream reference -> build baseline -> later refactoring experiments
```

The build-baseline commit, not the temporary locally built binary, should be
the comparison point for later experiments.
