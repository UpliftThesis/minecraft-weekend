/* Colour utilities (src/util/color.c) and cameras (src/util/camera.c).
 *
 * Only functions declared in color.h / camera.h are used. The Lab conversion
 * round trip is observed through rgb_brighten(c, 0) (rgb -> Lab -> rgb with an
 * unchanged lightness), so the internal rgb/xyz/lab helpers are not pinned.
 */
#include "check.h"
#include "harness.h"
#include "util/camera.h"
#include "gfx/window.h"

static double lum(vec3s c) { return 0.2126 * c.x + 0.7152 * c.y + 0.0722 * c.z; }
static vec4s v4(float r, float g, float b, float a) { return (vec4s) {{ r, g, b, a }}; }

/* ---------------- colour ---------------- */

TEST(rgbax2f_unpacks_hex_channels) {
    vec4s c = RGBAX2F(0xFF8000FFu);
    CHECK_NEAR(c.x, 1.0, 1e-6);
    CHECK_NEAR(c.y, 128 / 255.0, 1e-6);
    CHECK_NEAR(c.z, 0.0, 1e-6);
    CHECK_NEAR(c.w, 1.0, 1e-6);
    c = RGBAX2F(0x00000000u);
    CHECK(c.x == 0 && c.y == 0 && c.z == 0 && c.w == 0);
}

TEST(lab_round_trip_is_identity_over_rgb_grid) {
    for (int r = 0; r <= 10; r++) for (int g = 0; g <= 10; g++) for (int b = 0; b <= 10; b++) {
        vec3s c = {{ r / 10.0f, g / 10.0f, b / 10.0f }};
        vec3s rt = rgb_brighten(c, 0.0f);
        for (int i = 0; i < 3; i++)
            CHECK_MSG(fabs(rt.raw[i] - c.raw[i]) < 2e-3, "round trip of (%g,%g,%g) channel %d: %g",
                      c.x, c.y, c.z, i, rt.raw[i]);
    }
}

TEST(rgba_brighten_round_trip_keeps_alpha) {
    vec4s c = v4(0.2f, 0.4f, 0.6f, 0.3f);
    vec4s rt = rgba_brighten(c, 0.0f);
    for (int i = 0; i < 4; i++) CHECK_NEAR(rt.raw[i], c.raw[i], 2e-3);
    CHECK_NEAR(rgba_brighten(c, 0.1f).w, 0.3, 1e-6);
}

TEST(lab_lightness_of_black_and_grey_ramp) {
    vec3s black = rgb2lab((vec3s) {{ 0, 0, 0 }});
    CHECK_NEAR(black.x, 0.0, 1e-4);
    CHECK_NEAR(black.y, 0.5, 1e-4); /* a* = 0 is encoded as 0.5 */
    CHECK_NEAR(black.z, 0.5, 1e-4);
    double prev = -1;
    for (int i = 0; i <= 20; i++) {
        float g = i / 20.0f;
        double l = rgb2lab((vec3s) {{ g, g, g }}).x;
        CHECK_MSG(l > prev, "Lab lightness not increasing along the grey ramp at %g", g);
        prev = l;
    }
}

KNOWN_DEFECT(lab_reference_values_for_white_and_grey,
             "rgb2xyz/xyz2rgb build their sRGB<->XYZ matrices with cglm's column-major "
             "mat3s .col initialiser from row-major constants, so the matrices are transposed: "
             "rgb2lab(white) is ~(1.07, 0.14, 0.52) instead of (1, 0.5, 0.5) and greys are not neutral") {
    vec3s white = rgb2lab((vec3s) {{ 1, 1, 1 }});
    CHECK_NEAR(white.x, 1.0, 0.01);
    CHECK_NEAR(white.y, 0.5, 0.01);
    CHECK_NEAR(white.z, 0.5, 0.01);
    vec3s grey = rgb2lab((vec3s) {{ 0.5f, 0.5f, 0.5f }});
    CHECK_NEAR(grey.y, 0.5, 0.01);
    CHECK_NEAR(grey.z, 0.5, 0.01);
}

KNOWN_DEFECT(rgba_lerp_between_black_and_white_is_neutral_grey,
             "consequence of the transposed sRGB<->XYZ matrices: interpolating black->white "
             "(e.g. the sunset sunlight colour) passes through a tinted colour, "
             "rgba_lerp(black, white, 0.5) = ~(0.49, 0.45, 0.46)") {
    vec4s m = rgba_lerp(v4(0, 0, 0, 1), v4(1, 1, 1, 1), 0.5f);
    CHECK_NEAR(m.x, m.y, 0.01);
    CHECK_NEAR(m.y, m.z, 0.01);
}

TEST(rgba_lerp_endpoints_and_alpha) {
    vec4s cols[] = { v4(0, 0, 0, 1), v4(1, 1, 1, 1), v4(0.53f, 0.81f, 0.92f, 1), v4(1, 0.67f, 0.19f, 0.5f),
                     v4(0.01f, 0.01f, 0.02f, 0), v4(0.2f, 0.7f, 0.1f, 0.25f) };
    size_t n = sizeof cols / sizeof cols[0];
    for (size_t i = 0; i < n; i++) for (size_t j = 0; j < n; j++) {
        vec4s a = rgba_lerp(cols[i], cols[j], 0.0f), b = rgba_lerp(cols[i], cols[j], 1.0f);
        vec4s h = rgba_lerp(cols[i], cols[j], 0.5f);
        for (int k = 0; k < 4; k++) {
            CHECK_NEAR(a.raw[k], cols[i].raw[k], 2e-3);
            CHECK_NEAR(b.raw[k], cols[j].raw[k], 2e-3);
        }
        /* alpha interpolates linearly */
        CHECK_NEAR(h.w, (cols[i].w + cols[j].w) / 2, 1e-5);
    }
    /* lerp of a colour with itself is that colour for every t */
    for (int t = 0; t <= 10; t++) {
        vec4s c = rgba_lerp(cols[2], cols[2], t / 10.0f);
        for (int k = 0; k < 4; k++) CHECK_NEAR(c.raw[k], cols[2].raw[k], 2e-3);
    }
}

TEST(rgba_lerp_lightness_moves_monotonically_from_dark_to_light) {
    vec4s dark = v4(0.05f, 0.05f, 0.1f, 1), light = v4(0.9f, 0.85f, 0.7f, 1);
    double prev = -1;
    for (int t = 0; t <= 20; t++) {
        vec4s c = rgba_lerp(dark, light, t / 20.0f);
        double l = rgb2lab((vec3s) {{ c.x, c.y, c.z }}).x;
        CHECK_MSG(l >= prev - 1e-5, "lightness decreased at t=%d", t);
        prev = l;
    }
}

TEST(rgba_lerp3_passes_through_middle_colour) {
    vec4s a = v4(0, 0, 0, 1), b = v4(1, 0.79f, 0.49f, 1), c = v4(0.53f, 0.81f, 0.92f, 1);
    vec4s s = rgba_lerp3(a, b, c, 0.0f), m = rgba_lerp3(a, b, c, 0.5f), e = rgba_lerp3(a, b, c, 1.0f);
    for (int k = 0; k < 4; k++) {
        CHECK_NEAR(s.raw[k], a.raw[k], 2e-3);
        CHECK_NEAR(m.raw[k], b.raw[k], 2e-3);
        CHECK_NEAR(e.raw[k], c.raw[k], 2e-3);
    }
    /* first half follows a->b, second half b->c */
    vec4s q1 = rgba_lerp3(a, b, c, 0.25f), r1 = rgba_lerp(a, b, 0.5f);
    vec4s q3 = rgba_lerp3(a, b, c, 0.75f), r3 = rgba_lerp(b, c, 0.5f);
    for (int k = 0; k < 4; k++) {
        CHECK_NEAR(q1.raw[k], r1.raw[k], 1e-4);
        CHECK_NEAR(q3.raw[k], r3.raw[k], 1e-4);
    }
}

TEST(brighten_is_monotonic_in_amount) {
    vec3s cols[] = { {{ 0.4f, 0.5f, 0.3f }}, {{ 0.5f, 0.5f, 0.5f }}, {{ 0.2f, 0.3f, 0.6f }}, {{ 0.6f, 0.3f, 0.3f }} };
    for (size_t i = 0; i < 4; i++) {
        double prev = -1e9;
        for (int k = -2; k <= 2; k++) {
            double l = lum(rgb_brighten(cols[i], k * 0.1f));
            CHECK_MSG(l > prev, "colour %zu: brighten(%g) not brighter than previous", i, k * 0.1);
            prev = l;
        }
        CHECK(lum(rgb_brighten(cols[i], 0.1f)) > lum(cols[i]));
        CHECK(lum(rgb_brighten(cols[i], -0.1f)) < lum(cols[i]));
    }
}

/* ---------------- cameras ---------------- */

static vec4s clip(struct ViewProj vp, vec3s p) {
    return glms_mat4_mulv(glms_mat4_mul(vp.proj, vp.view), (vec4s) {{ p.x, p.y, p.z, 1.0f }});
}

static bool inside_clip(vec4s c) {
    return c.w > 0 && fabsf(c.x) <= c.w && fabsf(c.y) <= c.w && fabsf(c.z) <= c.w;
}

static bool finite_mat(mat4s m) {
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) if (!isfinite(m.raw[i][j])) return false;
    return true;
}

static void make_camera(struct PerspectiveCamera *c, vec3s pos, float yaw, float pitch) {
    window.size = (ivec2s) {{ 1280, 720 }};
    perspective_camera_init(c, radians(75.0f));
    c->aspect = 16.0f / 9.0f;
    c->position = pos;
    c->yaw = yaw;
    c->pitch = pitch;
    perspective_camera_update(c);
}

TEST(perspective_camera_matrices_are_well_formed) {
    struct PerspectiveCamera c;
    make_camera(&c, (vec3s) {{ 3, 4, 5 }}, 1.0f, 0.3f);
    CHECK(finite_mat(c.view_proj.view));
    CHECK(finite_mat(c.view_proj.proj));
    CHECK(c.znear > 0 && c.zfar > c.znear);
    CHECK_NEAR(glms_vec3_norm(c.direction), 1.0, 1e-5);
    CHECK_NEAR(glms_vec3_dot(c.direction, c.up), 0.0, 1e-5);
    CHECK_NEAR(glms_vec3_dot(c.direction, c.right), 0.0, 1e-5);
    CHECK_NEAR(glms_vec3_dot(c.up, c.right), 0.0, 1e-5);
    CHECK_NEAR(c.right.y, 0.0, 1e-6); /* no roll: right stays horizontal */
    CHECK(c.up.y > 0);
    /* view maps the camera position to the origin */
    vec4s o = glms_mat4_mulv(c.view_proj.view, (vec4s) {{ 3, 4, 5, 1 }});
    CHECK_NEAR(o.x, 0, 1e-4); CHECK_NEAR(o.y, 0, 1e-4); CHECK_NEAR(o.z, 0, 1e-4);
}

TEST(point_in_front_projects_inside_clip_space_and_behind_does_not) {
    test_rng_seed(77);
    for (int i = 0; i < 200; i++) {
        struct PerspectiveCamera c;
        vec3s pos = {{ (float) test_rng_range(-100, 100), (float) test_rng_range(-50, 150), (float) test_rng_range(-100, 100) }};
        float yaw = (float) (test_rng_range(0, 6283) / 1000.0), pitch = (float) (test_rng_range(-1500, 1500) / 1000.0);
        make_camera(&c, pos, yaw, pitch);
        vec3s ahead = glms_vec3_add(c.position, glms_vec3_scale(c.direction, 10.0f));
        vec3s behind = glms_vec3_sub(c.position, glms_vec3_scale(c.direction, 10.0f));
        vec4s a = clip(c.view_proj, ahead), b = clip(c.view_proj, behind);
        CHECK_MSG(inside_clip(a), "point ahead of camera %d not inside clip space", i);
        /* on the optical axis: centre of the screen */
        CHECK_NEAR(a.x / a.w, 0, 1e-3);
        CHECK_NEAR(a.y / a.w, 0, 1e-3);
        CHECK(!inside_clip(b));
        /* a point above the view axis lands in the upper half of the screen */
        vec4s u = clip(c.view_proj, glms_vec3_add(ahead, c.up));
        CHECK(inside_clip(u) && u.y / u.w > 0.01);
        /* nearer points have smaller depth than farther ones on the same ray */
        vec4s far_ = clip(c.view_proj, glms_vec3_add(c.position, glms_vec3_scale(c.direction, 100.0f)));
        CHECK(a.z / a.w < far_.z / far_.w);
        /* beyond the far plane is clipped */
        vec4s beyond = clip(c.view_proj, glms_vec3_add(c.position, glms_vec3_scale(c.direction, c.zfar * 2)));
        CHECK(!inside_clip(beyond));
    }
}

TEST(yaw_and_pitch_turn_the_view_direction) {
    struct PerspectiveCamera c;
    make_camera(&c, GLMS_VEC3_ZERO, 0.0f, 0.0f);
    vec3s d0 = c.direction;
    CHECK_NEAR(d0.y, 0, 1e-6);

    /* a quarter turn of yaw rotates the direction 90 degrees about +Y */
    make_camera(&c, GLMS_VEC3_ZERO, (float) (PI / 2), 0.0f);
    CHECK_NEAR(glms_vec3_dot(c.direction, d0), 0, 1e-5);
    CHECK_NEAR(c.direction.y, 0, 1e-6);
    make_camera(&c, GLMS_VEC3_ZERO, (float) PI, 0.0f);
    CHECK_NEAR(glms_vec3_dot(c.direction, d0), -1, 1e-5);

    /* positive pitch looks up, negative looks down; magnitude grows with pitch */
    float prev = -2;
    for (int k = -4; k <= 4; k++) {
        make_camera(&c, GLMS_VEC3_ZERO, 0.7f, k * 0.35f);
        CHECK(c.direction.y > prev);
        CHECK((k > 0) == (c.direction.y > 1e-6f));
        prev = c.direction.y;
    }

    /* pitch is limited to straight up/down */
    make_camera(&c, GLMS_VEC3_ZERO, 0.0f, 10.0f);
    CHECK(c.pitch <= (float) PI_2 + 1e-6f);
    make_camera(&c, GLMS_VEC3_ZERO, 0.0f, -10.0f);
    CHECK(c.pitch >= (float) -PI_2 - 1e-6f);
}

TEST(yaw_wraps_without_changing_direction) {
    struct PerspectiveCamera a, b;
    float yaws[] = { -0.5f, -3.0f, 7.0f, 13.0f, (float) TAU + 0.25f };
    for (int i = 0; i < 5; i++) {
        make_camera(&a, GLMS_VEC3_ZERO, yaws[i], 0.2f);
        float wrapped = fmodf(yaws[i], (float) TAU);
        if (wrapped < 0) wrapped += (float) TAU;
        make_camera(&b, GLMS_VEC3_ZERO, wrapped, 0.2f);
        CHECK(a.yaw >= 0 && a.yaw <= (float) TAU + 1e-5f);
        for (int k = 0; k < 3; k++) CHECK_NEAR(a.direction.raw[k], b.direction.raw[k], 1e-4);
    }
}

TEST(ortho_camera_maps_bounds_to_clip_corners) {
    struct OrthoCamera c;
    vec2s mn = {{ 0, 0 }}, mx = {{ 1280, 720 }};
    ortho_camera_init(&c, mn, mx);
    CHECK(finite_mat(c.view_proj.proj));
    vec4s lo = clip(c.view_proj, (vec3s) {{ mn.x, mn.y, 0 }});
    vec4s hi = clip(c.view_proj, (vec3s) {{ mx.x, mx.y, 0 }});
    vec4s mid = clip(c.view_proj, (vec3s) {{ 640, 360, 0 }});
    CHECK_NEAR(lo.x / lo.w, -1, 1e-5); CHECK_NEAR(lo.y / lo.w, -1, 1e-5);
    CHECK_NEAR(hi.x / hi.w, 1, 1e-5);  CHECK_NEAR(hi.y / hi.w, 1, 1e-5);
    CHECK_NEAR(mid.x / mid.w, 0, 1e-5); CHECK_NEAR(mid.y / mid.w, 0, 1e-5);
    CHECK(inside_clip(mid));
    /* UI layers at small positive/negative z stay inside the depth range */
    CHECK(inside_clip(clip(c.view_proj, (vec3s) {{ 640, 360, 50 }})));
    CHECK(inside_clip(clip(c.view_proj, (vec3s) {{ 640, 360, -50 }})));
}

int main(int argc, char **argv) { return run_tests(argc, argv); }
