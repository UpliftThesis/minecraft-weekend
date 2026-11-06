/* Smoke tests for the headless harness itself: worlds can be built, filled
 * with empty or generated chunks, and torn down. */
#include "check.h"
#include "harness.h"
TEST(empty_world_builds) {
    struct World *w = tw_create((ivec3s){{0,0,0}}, 2, 1);
    tw_fill_empty(w);
    CHECK_EQ(tw_loaded_count(w), 8);
    tw_destroy(w);
}
TEST(generated_world_builds) {
    struct World *w = tw_create((ivec3s){{0,0,0}}, 3, 42);
    tw_fill_generated(w);
    CHECK_EQ(tw_loaded_count(w), 27);
    size_t solid = 0;
    for (int x = -32; x < 64; x++) for (int z = -32; z < 64; z++) for (int y = -32; y < 64; y++)
        solid += world_get_block(w, (ivec3s){{x,y,z}}) != AIR;
    CHECK(solid > 0); /* generated terrain is not empty */
    tw_destroy(w);
}
int main(int argc, char **argv) { return run_tests(argc, argv); }
