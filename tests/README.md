# Headless regression tests

Run from the repository root:

```sh
./tests/run.sh
```

The suite compiles selected existing C sources with a C compiler (`CC`, default `cc` or `gcc`) and the standard math library. A POSIX shell and `mktemp` are required. Compilers and their temporary files and test binaries are confined to the new test directories; the runner removes its binaries after execution. No game assets, GPU, network access, or initialized Git submodules are required for these checks.

The tests exercise bitmap operations and contextual sorting, noise reproducibility and representative values, directional mappings, voxel ray traversal, world coordinate and storage operations, and entity/component lifecycle and event dispatch. See the individual test sources for cases. Some mathematical helpers compile with a small test-owned cglm compatibility header because the cglm submodule is absent; that shim is not a substitute for testing the game against the real dependency.

These are headless regression checks, not a full game test. They do not validate rendering, window/input handling, assets, live player behavior, full terrain generation, or lighting integration. The normal game build additionally needs initialized cglm and GLFW submodules and its documented build dependencies. Avoid using the in-place Makefile when existing source and dependency directories must remain untouched.

The ECS tests substitute small test components and registration for the real game systems. They exercise the actual ECS implementation but do not cover production component initialization or system integration.
