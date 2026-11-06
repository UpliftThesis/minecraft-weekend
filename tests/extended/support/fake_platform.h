/* Headless stand-ins for the platform layer (OpenGL via glad, GLFW).
 *
 * The game's OpenGL calls go through glad function pointers. fake_gl_install()
 * points every function the game uses at a recording no-op, so production code
 * that creates meshes, buffers or shaders can run without a GPU or window.
 *
 * The recorder exposes only coarse, API-level observations (what was uploaded
 * and drawn), which stay meaningful if the rendering code is restructured.
 */
#ifndef TEST_FAKE_PLATFORM_H
#define TEST_FAKE_PLATFORM_H

#include <stddef.h>

struct FakeGLStats {
    /* glDrawElements */
    size_t draw_calls;
    size_t drawn_indices;      /* sum of `count` over all draw calls */

    /* glBufferData, split by target */
    size_t array_uploads, element_uploads;
    size_t array_bytes, element_bytes;            /* totals */
    size_t last_array_bytes, last_element_bytes;  /* most recent upload */

    /* object lifetime bookkeeping (gen/create minus delete) */
    long live_buffers, live_vertex_arrays, live_textures, live_shaders, live_programs;
};

extern struct FakeGLStats fake_gl;

void fake_gl_install(void);
void fake_gl_reset_stats(void);

#endif
