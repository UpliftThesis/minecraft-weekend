/* Day/night cycle (src/world/sky.c) as a function of World::ticks.
 *
 * Observed through the public sky_render(): it computes the sky's sunlight,
 * fog and clear colours for the current world tick (the GL calls go to the
 * fake GL). Exact colours are deliberately not asserted; the tests check the
 * shape of the cycle: periodicity, day brighter than night, continuity, range.
 */
#include "check.h"
#include "harness.h"

struct SkySample { vec4s sun, clear, fog; };

static struct World *sky_world(void) {
    return tw_create((ivec3s) {{ 0, 0, 0 }}, 1, 1);
}

static struct SkySample sample(struct World *w, u64 ticks) {
    w->ticks = ticks;
    sky_render(&w->sky);
    return (struct SkySample) { w->sky.sunlight_color, w->sky.clear_color, w->sky.fog_color };
}

static double luminance(vec4s c) { return 0.2126 * c.x + 0.7152 * c.y + 0.0722 * c.z; }

static double max_channel_diff(vec4s a, vec4s b) {
    double m = 0;
    for (int i = 0; i < 4; i++) m = fmax(m, fabs((double) a.raw[i] - (double) b.raw[i]));
    return m;
}

static void check_in_unit_range(vec4s c, const char *what, u64 t) {
    for (int i = 0; i < 4; i++)
        CHECK_MSG(c.raw[i] >= -1e-3f && c.raw[i] <= 1.0f + 1e-3f && !isnan(c.raw[i]),
                  "%s channel %d = %f out of [0,1] at tick %llu", what, i, c.raw[i], (unsigned long long) t);
}

/* Ticks that cover the whole cycle: dense around the two transitions
 * (start of day and start of night), coarse elsewhere. */
static size_t cycle_ticks(u64 *out, size_t cap) {
    size_t n = 0;
    const u64 window = 3000; /* > the sunrise/sunset length used by the game */
    for (u64 t = 0; t < TOTAL_DAY_TICKS && n < cap; ) {
        out[n++] = t;
        bool near = t < window || t > TOTAL_DAY_TICKS - window ||
                    (t > DAY_TICKS - window && t < DAY_TICKS + window);
        t += near ? 1 : 97;
    }
    return n;
}

#define CAP 20000
static u64 ticks_buf[CAP];

TEST(sky_cycle_is_periodic_in_total_day_ticks) {
    struct World *w = sky_world();
    u64 probes[] = { 0, 1, 700, 1349, 1351, DAY_TICKS / 2, DAY_TICKS - 5, DAY_TICKS, DAY_TICKS + 900,
                     DAY_TICKS + NIGHT_TICKS / 2, TOTAL_DAY_TICKS - 1 };
    for (size_t i = 0; i < sizeof probes / sizeof probes[0]; i++) {
        struct SkySample a = sample(w, probes[i]);
        for (u64 k = 1; k <= 3; k++) {
            struct SkySample b = sample(w, probes[i] + k * TOTAL_DAY_TICKS);
            CHECK_MSG(max_channel_diff(a.sun, b.sun) < 1e-6 && max_channel_diff(a.clear, b.clear) < 1e-6 &&
                      max_channel_diff(a.fog, b.fog) < 1e-6,
                      "sky at tick %llu differs from tick + %llu days", (unsigned long long) probes[i],
                      (unsigned long long) k);
        }
    }
    tw_destroy(w);
}

TEST(day_is_brighter_than_night) {
    struct World *w = sky_world();
    struct SkySample noon = sample(w, DAY_TICKS / 2);
    struct SkySample midnight = sample(w, DAY_TICKS + NIGHT_TICKS / 2);
    CHECK(luminance(noon.sun) > luminance(midnight.sun) + 0.5);
    CHECK(luminance(noon.clear) > luminance(midnight.clear) + 0.2);
    CHECK(luminance(noon.fog) > luminance(midnight.fog) + 0.2);

    /* throughout the middle of the day / night the sky is steady */
    for (u64 t = DAY_TICKS / 4; t < 3 * DAY_TICKS / 4; t += 1013) {
        struct SkySample s = sample(w, t);
        CHECK(max_channel_diff(s.sun, noon.sun) < 1e-4);
        CHECK(max_channel_diff(s.clear, noon.clear) < 1e-4);
    }
    for (u64 t = DAY_TICKS + NIGHT_TICKS / 4; t < DAY_TICKS + 3 * NIGHT_TICKS / 4; t += 1013) {
        struct SkySample s = sample(w, t);
        CHECK(max_channel_diff(s.sun, midnight.sun) < 1e-4);
        CHECK(max_channel_diff(s.clear, midnight.clear) < 1e-4);
    }
    tw_destroy(w);
}

TEST(sky_colours_stay_in_unit_range) {
    struct World *w = sky_world();
    size_t n = cycle_ticks(ticks_buf, CAP);
    CHECK(n < CAP);
    for (size_t i = 0; i < n; i++) {
        struct SkySample s = sample(w, ticks_buf[i]);
        check_in_unit_range(s.sun, "sunlight", ticks_buf[i]);
        check_in_unit_range(s.clear, "clear", ticks_buf[i]);
        check_in_unit_range(s.fog, "fog", ticks_buf[i]);
    }
    tw_destroy(w);
}

TEST(sky_transitions_are_continuous) {
    struct World *w = sky_world();
    /* every tick through sunset and through sunrise (including the wrap of the
     * day counter), consecutive ticks must change colours only slightly */
    const u64 window = 3000;
    u64 starts[] = { DAY_TICKS - window, TOTAL_DAY_TICKS - window };
    double biggest_change = 0;
    for (size_t k = 0; k < 2; k++) {
        struct SkySample prev = sample(w, starts[k]);
        for (u64 t = starts[k] + 1; t <= starts[k] + 2 * window; t++) {
            struct SkySample s = sample(w, t);
            double d = fmax(max_channel_diff(prev.sun, s.sun),
                            fmax(max_channel_diff(prev.clear, s.clear), max_channel_diff(prev.fog, s.fog)));
            CHECK_MSG(d < 0.02, "sky colour jumps by %f between ticks %llu and %llu", d,
                      (unsigned long long) (t - 1), (unsigned long long) t);
            biggest_change = fmax(biggest_change, d);
            prev = s;
        }
    }
    CHECK(biggest_change > 0); /* the windows really contain a transition */
    tw_destroy(w);
}

TEST(sunlight_fades_monotonically_at_sunset_and_rises_at_sunrise) {
    struct World *w = sky_world();
    const u64 window = 3000;
    /* 1e-4 slack: the colour lerp round-trips through Lab in f32 */
    double prev = luminance(sample(w, DAY_TICKS - window).sun);
    double first = prev;
    for (u64 t = DAY_TICKS - window + 1; t <= DAY_TICKS + window; t++) {
        double l = luminance(sample(w, t).sun);
        CHECK_MSG(l <= prev + 1e-4, "sunlight brightens during sunset at tick %llu", (unsigned long long) t);
        prev = l;
    }
    CHECK(prev < first - 0.5);

    prev = luminance(sample(w, TOTAL_DAY_TICKS - window).sun);
    first = prev;
    for (u64 t = TOTAL_DAY_TICKS - window + 1; t <= TOTAL_DAY_TICKS + window; t++) {
        double l = luminance(sample(w, t).sun);
        CHECK_MSG(l >= prev - 1e-4, "sunlight darkens during sunrise at tick %llu", (unsigned long long) t);
        prev = l;
    }
    CHECK(prev > first + 0.5);
    tw_destroy(w);
}

TEST(sky_render_draws_and_sets_clear_colour_to_fog) {
    struct World *w = sky_world();
    u64 probes[] = { DAY_TICKS / 2, DAY_TICKS + NIGHT_TICKS / 2, DAY_TICKS };
    for (size_t i = 0; i < 3; i++) {
        fake_gl_reset_stats();
        struct SkySample s = sample(w, probes[i]);
        CHECK(fake_gl.draw_calls > 0);
        /* the screen clear colour matches the fog so distant terrain blends into it */
        CHECK(max_channel_diff(s.clear, s.fog) < 1e-6);
    }
    tw_destroy(w);
}

int main(int argc, char **argv) { return run_tests(argc, argv); }
