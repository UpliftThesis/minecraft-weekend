#include <math.h>
#include <stdio.h>
#include "noise1234.h"

static int failures;

static void close_to(const char *name, float actual, float expected, float tolerance) {
    if (!isfinite(actual) || fabsf(actual - expected) > tolerance) {
        fprintf(stderr, "%s: got %.9g, expected %.9g (+/- %.9g)\n",
                name, actual, expected, tolerance);
        failures++;
    }
}

int main(void) {
    /* These samples protect generated terrain against changes to the underlying
       noise field. Tolerances allow small differences in floating point math. */
    close_to("noise3 positive sample", noise3(0.1f, 0.2f, 0.3f), 0.371087819f, 1e-5f);
    close_to("noise3 negative sample", noise3(-1.25f, 3.5f, 9.75f), 0.360677540f, 1e-5f);
    close_to("noise3 lattice point", noise3(13.0f, -7.0f, 2.0f), 0.0f, 1e-6f);
    close_to("noise1 sample", noise1(0.375f), 0.376446068f, 1e-5f);
    close_to("noise2 sample", noise2(-1.25f, 0.625f), -0.142863438f, 1e-5f);

    for (int i = -12; i <= 12; i++) {
        float x = (float)i / 4.0f + 0.13f;
        float y = (float)i / 7.0f - 0.21f;
        float z = (float)i / 3.0f + 0.37f;
        float value = noise3(x, y, z);
        close_to("deterministic noise3", noise3(x, y, z), value, 0.0f);
        if (!isfinite(value) || fabsf(value) > 1.1f) {
            fprintf(stderr, "noise3 range: sample %d produced %.9g\n", i, value);
            failures++;
        }
        close_to("noise3 continuity", noise3(x + 0.0001f, y, z), value, 0.002f);
    }

    /* Periodic 3D noise repeats in each coordinate, independently. */
    float p = pnoise3(1.25f, 2.5f, 3.75f, 5, 7, 9);
    close_to("periodic x", pnoise3(6.25f, 2.5f, 3.75f, 5, 7, 9), p, 1e-6f);
    close_to("periodic y", pnoise3(1.25f, 9.5f, 3.75f, 5, 7, 9), p, 1e-6f);
    close_to("periodic z", pnoise3(1.25f, 2.5f, 12.75f, 5, 7, 9), p, 1e-6f);

    if (failures) {
        fprintf(stderr, "%d noise assertion(s) failed\n", failures);
        return 1;
    }
    puts("Noise regression checks passed");
    return 0;
}
