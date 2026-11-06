# Math regression tests

Run `tests/math/run.sh` from the repository root (or invoke it from another working directory). The test compiles the production `fmath.c` and `direction.c` with a small, test-only cglm stand-in because this checkout lacks the cglm submodule. The stand-in supplies only vector layouts and component-wise division/norm required by these functions; it does not test integration with the real dependency. Once cglm is available, a separate real-dependency build should also be run.

These cases cover directions and block ray traversal, including both signs on each axis, negative coordinates, an occupied starting voxel, distance cutoffs, a non-unit direction and a slanted ray. They do not define behavior for a zero-length ray, a start exactly on a grid plane, or ambiguous edge/corner intersections. A starting-voxel hit has no asserted face.

The runner also exercises production world-coordinate and packed-storage header APIs. Graphics, unrelated entity types and `chunk_on_modify` are replaced by small stand-ins, so lighting propagation, heightmap updates and mesh invalidation are outside this suite. The storage checks avoid enshrining a discovered defect: updating torchlight currently clears a previously set sunlight value, apparently because of the width of the complemented torchlight mask. This should be addressed as a behavior decision during refactoring.
