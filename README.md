# Minecraft, but I made it in 48 hours*

\* I've actually updated it since - [see this commit for the 48 hour version](https://github.com/jdah/minecraft-weekend/tree/cb19738305804b5734faa7118c1c784f26ff9463).

![screenshot](screenshots/1.png)

#### Features:
- Infinite, procedurally generated world
- Infinite height/depth
- Day/night cycle
- Biomes
- ECS-driven player and entities with full collision and movement
- Full RGB lighting
- Full transparency + translucency support
- Sprite blocks (flowers)
- Animated blocks (water + lava)
- Distance fog
- A whole lot of different block types
- More

#### Building

##### Unix-like

Required tools: `clang`, `make`, and `cmake` (3.13 or newer). On macOS, install the
Xcode Command Line Tools (`xcode-select --install`) for `clang` and `make`, and
get `cmake` from Homebrew (`brew install cmake`) or cmake.org. CMake is not
vendored in this repository.

`$ git clone --recurse-submodules https://github.com/jdah/minecraft-weekend.git`\
`$ make`

In an existing clone, fetch the submodules (CGLM and GLFW) with
`$ git submodule update --init --recursive`.

`make` builds the dependencies and then the game. All generated files go to
`build/` and `bin/`; nothing is written into `lib/` or `src/`, so the submodules
stay clean. Running `make` again without changes does nothing.

- GLFW is built out-of-source with CMake as a static library only (`build/glfw/src/libglfw3.a`).
- GLAD (`build/glad/glad.o`) and libnoise (`build/noise/libnoise.a`) are compiled directly into `build/`.
- CGLM is used header-only; its compiled library is not built or linked.

Other targets: `$ make libs` builds only the dependencies, `$ make run` builds
and starts the game, and `$ make clean` removes `build/` and `bin/`.

The game binary, once built with `$ make`, can be found in `./bin/`.

*Be sure* to run with `$ ./bin/game` out of the root directory of the repository.
If you are getting "cannot open file" errors (such as "cannot find ./res/shaders/*.vs"), this is the issue. 

##### Windows

good luck 🤷‍♂️ probably try building under WSL and using an X environment to pass graphics through.
