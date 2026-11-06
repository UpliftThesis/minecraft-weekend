#include <math.h>
#include <stdio.h>
#include "src/world/gen/noise.h"

static int failures;

static void close_to(const char *label, float actual, float expected) {
    if (!isfinite(actual) || fabsf(actual - expected) > 1e-5f) {
        fprintf(stderr, "%s: got %.9g, expected %.9g\n", label, actual, expected);
        failures++;
    }
}

static float sample(struct Noise *n, float seed, float x, float z) {
    return n->compute(n->params, seed, x, z);
}

int main(void) {
    struct Noise base = basic(3);
    struct Noise octaves = octave(4, -2);
    struct Noise combined_noise = combined(&base, &octaves);
    struct Noise scaled = expscale(&base, 2.0f, 0.25f);
    struct Noise empty = octave(0, 0);
    struct Noise identity = expscale(&base, 1.0f, 1.0f);
    float seed = 11.0f, x = 1.25f, z = -0.75f;

    /* Baselines characterize the actual terrain noise objects and their
       composition, while allowing minor floating point differences. */
    close_to("basic noise", sample(&base, seed, x, z), 0.158824608f);
    close_to("octave noise", sample(&octaves, seed, x, z), 0.264242709f);
    close_to("combined noise", sample(&combined_noise, seed, x, z), 0.0988365486f);
    close_to("scaled noise", sample(&scaled, seed, x, z), 0.002649097f);
    close_to("zero octaves", sample(&empty, seed, x, z), 0.0f);
    close_to("identity scaling", sample(&identity, seed, x, z), sample(&base, seed, x, z));
    for (int i = -6; i <= 6; i++) {
        x = i * 0.33f;
        z = i * -0.71f;
        float value = sample(&combined_noise, seed, x, z);
        close_to("deterministic combined noise", sample(&combined_noise, seed, x, z), value);
        close_to("identity scaling at varied coordinates", sample(&identity, seed, x, z), sample(&base, seed, x, z));
    }
    if (fabsf(sample(&base, 11.0f, 1.25f, -0.75f) - sample(&base, 12.0f, 1.25f, -0.75f)) < 1e-5f) {
        fputs("changing the seed did not change the sampled field\n", stderr);
        failures++;
    }
    if (failures) {
        fprintf(stderr, "%d world noise assertion(s) failed\n", failures);
        return 1;
    }
    puts("World noise regression checks passed");
    return 0;
}
