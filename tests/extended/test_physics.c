/* Regression tests for entity physics and movement (src/entity/c_physics.c,
 * c_position.c, c_movement.c) and the AABB helpers in src/util/aabb.h.
 *
 * Entities are created on the real ECS of a headless world the same way
 * src/main.c creates the player, and simulated with ECS_TICK events.
 * Assertions are about observable motion (falls, lands on top, is blocked,
 * passes through non-solid blocks), not about exact trajectories. */
#include "check.h"
#include "harness.h"

#define V3(x, y, z) ((ivec3s){{ (x), (y), (z) }})
#define F3(x, y, z) ((vec3s){{ (x), (y), (z) }})

/* tolerance for "resting on" / "touching" */
#define REST_TOL 0.05f
/* how far an entity may overlap a solid block before we call it "inside" */
#define OVERLAP_TOL 1e-3f

/* player-sized box, as in src/main.c */
static const vec3s PLAYER_SIZE = {{ 0.2f, 1.6f, 0.2f }};

/* ---- helpers --------------------------------------------------------- */

static struct World *flat_world(void) {
    struct World *w = tw_create(V3(0, 0, 0), 3, 1);
    tw_fill_empty(w);
    return w;
}

static void fill_box(struct World *w, ivec3s lo, ivec3s hi, enum BlockId b) {
    for (int x = lo.x; x <= hi.x; x++) for (int y = lo.y; y <= hi.y; y++) for (int z = lo.z; z <= hi.z; z++)
        world_set_block(w, V3(x, y, z), b);
}

/* entity with position + physics, `pos` is the centre of its box */
static struct Entity spawn(struct World *w, vec3s pos, vec3s size, bool gravity, bool collide) {
    struct Entity e = ecs_new(&w->ecs);
    ecs_add(e, C_POSITION, ((struct PositionComponent) { .position = pos }));
    ecs_add(e, C_PHYSICS, ((struct PhysicsComponent) {
        .size = { F3(0, 0, 0), size },
        .flags = { .gravity = gravity, .collide = collide }
    }));
    return e;
}

static vec3s pos_of(struct Entity e) { return ((struct PositionComponent *)ecs_get(e, C_POSITION))->position; }
static struct PhysicsComponent *phys(struct Entity e) { return ecs_get(e, C_PHYSICS); }
static void tick(struct World *w) { ecs_event(&w->ecs, ECS_TICK); }

/* world-space box of an entity: centred on its position */
static void entity_box(struct Entity e, AABB out) {
    vec3s half = glms_vec3_scale(phys(e)->size[1], 0.5f), p = pos_of(e);
    out[0] = glms_vec3_sub(p, half);
    out[1] = glms_vec3_add(p, half);
}

/* largest overlap depth (min over axes) of the entity with any solid block */
static float max_penetration(struct World *w, struct Entity e) {
    AABB b;
    entity_box(e, b);
    float worst = 0;
    for (int x = (int)floorf(b[0].x); x <= (int)floorf(b[1].x); x++)
        for (int y = (int)floorf(b[0].y); y <= (int)floorf(b[1].y); y++)
            for (int z = (int)floorf(b[0].z); z <= (int)floorf(b[1].z); z++) {
                if (!BLOCKS[world_get_block(w, V3(x, y, z))].solid) continue;
                float dx = fminf(b[1].x, x + 1.0f) - fmaxf(b[0].x, (float)x);
                float dy = fminf(b[1].y, y + 1.0f) - fmaxf(b[0].y, (float)y);
                float dz = fminf(b[1].z, z + 1.0f) - fmaxf(b[0].z, (float)z);
                float d = fminf(dx, fminf(dy, dz));
                if (d > worst) worst = d;
            }
    return worst;
}

/* ---- aabb.h ---------------------------------------------------------- */

TEST(aabb_depth_of_overlapping_boxes) {
    AABB a = { F3(0, 0, 0), F3(2, 2, 2) };
    AABB b = { F3(1.5f, 1.0f, -1.0f), F3(3, 4, 0.25f) };
    vec3s d = glms_aabb_aabb_depth(a, b);
    CHECK_NEAR(d.x, 0.5f, 1e-6);
    CHECK_NEAR(d.y, 1.0f, 1e-6);
    CHECK_NEAR(d.z, 0.25f, 1e-6);
    /* symmetric in magnitude */
    vec3s e = glms_aabb_aabb_depth(b, a);
    CHECK_NEAR(e.x, d.x, 1e-6);
    CHECK_NEAR(e.y, d.y, 1e-6);
    CHECK_NEAR(e.z, d.z, 1e-6);
}

TEST(aabb_depth_property_random) {
    test_rng_seed(17);
    for (int i = 0; i < 1000; i++) {
        AABB a, b;
        for (int k = 0; k < 3; k++) {
            float a0 = test_rng_range(-500, 500) / 100.0f, as = test_rng_range(1, 300) / 100.0f;
            float b0 = test_rng_range(-500, 500) / 100.0f, bs = test_rng_range(1, 300) / 100.0f;
            a[0].raw[k] = a0; a[1].raw[k] = a0 + as;
            b[0].raw[k] = b0; b[1].raw[k] = b0 + bs;
        }
        if (!glms_aabb_aabb(a, b)) continue;
        vec3s d = glms_aabb_aabb_depth(a, b);
        for (int k = 0; k < 3; k++) {
            float overlap = fminf(a[1].raw[k], b[1].raw[k]) - fmaxf(a[0].raw[k], b[0].raw[k]);
            float sa = a[1].raw[k] - a[0].raw[k], sb = b[1].raw[k] - b[0].raw[k];
            /* depth is the distance needed to separate along that axis toward the
             * nearer side: at least the overlap, never more than both boxes' extents */
            CHECK(d.raw[k] >= overlap - 1e-5f);
            CHECK(d.raw[k] <= sa + sb + 1e-5f);
        }
    }
}

TEST(aabb_scale_keeps_centre) {
    AABB a = { F3(1, 2, 3), F3(3, 6, 4) };
    AABB s;
    glms_aabb_scale(a, F3(2, 0.5f, 3), s);
    CHECK_NEAR(s[0].x, 0, 1e-6); CHECK_NEAR(s[1].x, 4, 1e-6);
    CHECK_NEAR(s[0].y, 3, 1e-6); CHECK_NEAR(s[1].y, 5, 1e-6);
    CHECK_NEAR(s[0].z, 2, 1e-6); CHECK_NEAR(s[1].z, 5, 1e-6);
    /* in-place scaling works too (physics uses it that way) */
    glms_aabb_scale(a, F3(1, 1, 1), a);
    CHECK_NEAR(a[0].x, 1, 1e-6); CHECK_NEAR(a[1].y, 6, 1e-6); CHECK_NEAR(a[1].z, 4, 1e-6);
}

TEST(aabb_intersection_includes_touching) {
    AABB a = { F3(0, 0, 0), F3(1, 1, 1) };
    AABB b = { F3(1, 0, 0), F3(2, 1, 1) };
    AABB c = { F3(1.01f, 0, 0), F3(2, 1, 1) };
    CHECK(glms_aabb_aabb(a, b));
    CHECK(!glms_aabb_aabb(a, c));
}

/* ---- falling and landing --------------------------------------------- */

TEST(entity_falls_and_rests_on_floor) {
    struct World *w = flat_world();
    fill_box(w, V3(-4, 0, -4), V3(4, 0, 4), STONE);  /* floor top at y = 1 */
    struct Entity e = spawn(w, F3(0.5f, 6.0f, 0.5f), PLAYER_SIZE, true, true);

    float prev_y = pos_of(e).y;
    bool moved_down = false;
    for (int t = 0; t < 400; t++) {
        tick(w);
        float y = pos_of(e).y;
        CHECK_MSG(y <= prev_y + 1e-6f, "tick %d: entity rose from %f to %f", t, prev_y, y);
        moved_down |= y < prev_y;
        prev_y = y;
        CHECK_MSG(max_penetration(w, e) <= OVERLAP_TOL, "tick %d: entity inside the floor", t);
    }
    CHECK(moved_down);
    float bottom = pos_of(e).y - PLAYER_SIZE.y / 2;
    CHECK_MSG(bottom >= 1.0f - OVERLAP_TOL && bottom <= 1.0f + REST_TOL, "rests at bottom y=%f", bottom);
    CHECK_NEAR(phys(e)->velocity.y, 0, 1e-6);
    CHECK(phys(e)->flags.grounded);
    /* stays at rest */
    float rest = pos_of(e).y;
    for (int t = 0; t < 50; t++) tick(w);
    CHECK_NEAR(pos_of(e).y, rest, 1e-5);
    /* x/z did not drift */
    CHECK_NEAR(pos_of(e).x, 0.5f, 1e-5);
    CHECK_NEAR(pos_of(e).z, 0.5f, 1e-5);
    tw_destroy(w);
}

TEST(entity_lands_on_top_of_floor_across_chunk_border) {
    /* the floor block is in chunk y=-1, the entity starts in chunk y=0 */
    struct World *w = flat_world();
    fill_box(w, V3(-3, -1, -3), V3(3, -1, 3), DIRT);  /* top at y = 0 */
    struct Entity e = spawn(w, F3(-0.3f, 10.0f, 0.7f), F3(0.6f, 1.8f, 0.6f), true, true);
    for (int t = 0; t < 500; t++) {
        tick(w);
        CHECK_MSG(max_penetration(w, e) <= OVERLAP_TOL, "tick %d: entity inside the floor", t);
    }
    float bottom = pos_of(e).y - 0.9f;
    CHECK_MSG(bottom >= -OVERLAP_TOL && bottom <= REST_TOL, "rests at bottom y=%f", bottom);
    CHECK(phys(e)->flags.grounded);
    tw_destroy(w);
}

TEST(entity_without_floor_keeps_falling_and_accelerates) {
    struct World *w = flat_world();
    struct Entity e = spawn(w, F3(0.5f, 40.0f, 0.5f), PLAYER_SIZE, true, true);
    float y0 = pos_of(e).y, prev = y0, prev_step = 0;
    for (int t = 0; t < 60; t++) {
        tick(w);
        float step = prev - pos_of(e).y;
        CHECK_MSG(step > prev_step, "tick %d: fall speed did not increase (%g <= %g)", t, step, prev_step);
        prev_step = step;
        prev = pos_of(e).y;
    }
    CHECK(pos_of(e).y < y0 - 5.0f);
    CHECK(phys(e)->velocity.y < 0);
    CHECK(!phys(e)->flags.grounded);
    CHECK_NEAR(pos_of(e).x, 0.5f, 1e-6);
    tw_destroy(w);
}

TEST(entity_without_gravity_stays_put) {
    struct World *w = flat_world();
    struct Entity e = spawn(w, F3(0.5f, 10.0f, 0.5f), PLAYER_SIZE, false, true);
    for (int t = 0; t < 30; t++) tick(w);
    CHECK_NEAR(pos_of(e).y, 10.0f, 1e-6);
    tw_destroy(w);
}

TEST(falling_through_water_is_slower_and_lands_on_bottom) {
    struct World *w = flat_world();
    fill_box(w, V3(-3, 0, -3), V3(3, 0, 3), SAND);     /* bed, top y=1 */
    fill_box(w, V3(-3, 1, -3), V3(3, 12, 3), WATER);   /* deep water above */
    struct Entity in_water = spawn(w, F3(0.5f, 12.0f, 0.5f), PLAYER_SIZE, true, true);
    struct Entity in_air = spawn(w, F3(10.5f, 12.0f, 10.5f), PLAYER_SIZE, true, true);
    for (int t = 0; t < 20; t++) tick(w);
    float fell_water = 12.0f - pos_of(in_water).y, fell_air = 12.0f - pos_of(in_air).y;
    CHECK(fell_water > 0);
    CHECK_MSG(fell_water < fell_air, "water %f vs air %f", fell_water, fell_air);
    for (int t = 0; t < 1000; t++) tick(w);
    float bottom = pos_of(in_water).y - PLAYER_SIZE.y / 2;
    CHECK_MSG(bottom >= 1.0f - OVERLAP_TOL && bottom <= 1.0f + REST_TOL, "rests at bottom y=%f", bottom);
    tw_destroy(w);
}

TEST(falls_through_non_solid_blocks) {
    struct World *w = flat_world();
    fill_box(w, V3(-2, 0, -2), V3(2, 0, 2), STONE);  /* top y=1 */
    enum BlockId soft[] = { TORCH, ROSE, BUTTERCUP, TALLGRASS, SHRUB, WATER, LAVA };
    for (int i = 0; i < 7; i++) world_set_block(w, V3(0, 2 + i, 0), soft[i]);
    struct Entity e = spawn(w, F3(0.5f, 12.0f, 0.5f), PLAYER_SIZE, true, true);
    for (int t = 0; t < 1500; t++) tick(w);
    float bottom = pos_of(e).y - PLAYER_SIZE.y / 2;
    CHECK_MSG(bottom >= 1.0f - OVERLAP_TOL && bottom <= 1.0f + REST_TOL, "rests at bottom y=%f", bottom);
    tw_destroy(w);
}

/* ---- horizontal collisions -------------------------------------------- */

TEST(wall_stops_horizontal_movement) {
    struct World *w = flat_world();
    fill_box(w, V3(-4, 0, -4), V3(8, 0, 4), STONE);   /* floor, top y=1 */
    fill_box(w, V3(4, 1, -4), V3(4, 4, 4), STONE);    /* wall face at x=4 */
    struct Entity e = spawn(w, F3(0.5f, 1.0f + 0.8f + 0.01f, 0.5f), PLAYER_SIZE, true, true);
    for (int t = 0; t < 200; t++) {
        phys(e)->velocity.x = 0.15f;   /* keep pushing towards the wall */
        tick(w);
        CHECK_MSG(max_penetration(w, e) <= OVERLAP_TOL, "tick %d: entity inside a block", t);
    }
    float front = pos_of(e).x + PLAYER_SIZE.x / 2;
    CHECK_MSG(front <= 4.0f + OVERLAP_TOL && front >= 4.0f - REST_TOL, "front at x=%f", front);
    CHECK(phys(e)->stopped.x);
    tw_destroy(w);
}

TEST(wall_stops_movement_in_negative_direction) {
    struct World *w = flat_world();
    fill_box(w, V3(-6, 0, -6), V3(4, 0, 4), STONE);
    fill_box(w, V3(-6, 1, -3), V3(4, 3, -3), STONE);  /* wall, its +z face at z=-2 */
    struct Entity e = spawn(w, F3(0.5f, 1.81f, 0.5f), PLAYER_SIZE, true, true);
    for (int t = 0; t < 200; t++) {
        phys(e)->velocity.z = -0.15f;
        tick(w);
        CHECK(max_penetration(w, e) <= OVERLAP_TOL);
    }
    float back = pos_of(e).z - PLAYER_SIZE.z / 2;
    CHECK_MSG(back >= -2.0f - OVERLAP_TOL && back <= -2.0f + REST_TOL, "back at z=%f", back);
    tw_destroy(w);
}

TEST(physics_move_reports_actual_displacement) {
    struct World *w = flat_world();
    fill_box(w, V3(3, 0, -2), V3(3, 5, 2), STONE);  /* wall face at x=3 */
    struct Entity e = spawn(w, F3(1.5f, 2.5f, 0.5f), F3(1, 1, 1), false, true);
    /* free move */
    vec3s before = pos_of(e);
    vec3s m = physics_move(phys(e), e, F3(0.25f, 0, 0));
    CHECK_NEAR(m.x, 0.25f, 1e-5);
    CHECK_NEAR(pos_of(e).x - before.x, m.x, 1e-6);
    /* blocked move: box front at 2.25, wall at 3 -> at most 0.75 */
    before = pos_of(e);
    m = physics_move(phys(e), e, F3(0.9f, 0, 0));
    CHECK(m.x < 0.75f + 1e-5f);
    CHECK(m.x > 0.75f - REST_TOL);
    CHECK_NEAR(pos_of(e).x - before.x, m.x, 1e-6);
    CHECK_NEAR(m.y, 0, 1e-6);
    CHECK_NEAR(m.z, 0, 1e-6);
    /* moving further into the wall does not move it */
    before = pos_of(e);
    m = physics_move(phys(e), e, F3(0.3f, 0, 0));
    CHECK(m.x <= 1e-5f);
    CHECK(pos_of(e).x <= before.x + 1e-5f);
    /* sliding along the wall still works */
    m = physics_move(phys(e), e, F3(0.3f, 0, 0.4f));
    CHECK_NEAR(m.z, 0.4f, 1e-5);
    CHECK(max_penetration(w, e) <= OVERLAP_TOL);
    tw_destroy(w);
}

TEST(movement_through_non_solid_blocks_is_free) {
    struct World *w = flat_world();
    enum BlockId soft[] = { WATER, TORCH, ROSE, TALLGRASS, SHRUB, BUTTERCUP, LAVA, AIR };
    for (int i = 0; i < 8; i++) fill_box(w, V3(2 + i, 1, -1), V3(2 + i, 4, 1), soft[i]);
    struct Entity e = spawn(w, F3(0.5f, 2.5f, 0.5f), PLAYER_SIZE, false, true);
    float total = 0;
    for (int i = 0; i < 40; i++) total += physics_move(phys(e), e, F3(0.25f, 0, 0)).x;
    CHECK_NEAR(total, 10.0f, 1e-3);
    CHECK_NEAR(pos_of(e).x, 10.5f, 1e-3);
    tw_destroy(w);
}

TEST(non_colliding_entity_passes_through_walls) {
    struct World *w = flat_world();
    fill_box(w, V3(2, 0, -2), V3(2, 5, 2), STONE);
    struct Entity e = spawn(w, F3(0.5f, 2.5f, 0.5f), PLAYER_SIZE, false, false);
    vec3s m = physics_move(phys(e), e, F3(3.0f, 0, 0));
    CHECK_NEAR(m.x, 3.0f, 1e-6);
    CHECK_NEAR(pos_of(e).x, 3.5f, 1e-6);
    tw_destroy(w);
}

TEST(physics_collides_with_box) {
    struct World *w = flat_world();
    struct Entity e = spawn(w, F3(0.5f, 2.5f, 0.5f), F3(1, 1, 1), false, true);
    physics_move(phys(e), e, F3(0, 0, 0));   /* refresh the entity's box */
    AABB touching = { F3(1, 2, 0), F3(2, 3, 1) };
    AABB overlapping = { F3(0.8f, 2.8f, 0.8f), F3(3, 3, 3) };
    AABB apart = { F3(1.5f, 2, 0), F3(2.5f, 3, 1) };
    CHECK(physics_collides(phys(e), overlapping));
    CHECK(physics_collides(phys(e), touching));
    CHECK(!physics_collides(phys(e), apart));
    /* after moving away the old box no longer collides */
    physics_move(phys(e), e, F3(-3, 0, 0));
    CHECK(!physics_collides(phys(e), overlapping));
    tw_destroy(w);
}

TEST(position_tick_tracks_block_and_chunk) {
    struct World *w = flat_world();
    struct Entity e = spawn(w, F3(-0.5f, 31.5f, 32.25f), PLAYER_SIZE, false, false);
    tick(w);
    struct PositionComponent *p = ecs_get(e, C_POSITION);
    CHECK_IVEC3_EQ(p->block, V3(-1, 31, 32));
    CHECK_IVEC3_EQ(p->offset, V3(-1, 0, 1));
    CHECK(p->block_changed);
    CHECK(p->offset_changed);
    tick(w);
    CHECK(!p->block_changed);
    CHECK(!p->offset_changed);
    p->position = F3(-0.5f, 32.5f, 32.25f);
    tick(w);
    CHECK_IVEC3_EQ(p->block, V3(-1, 32, 32));
    CHECK_IVEC3_EQ(p->offset, V3(-1, 1, 1));
    CHECK(p->block_changed && p->offset_changed);
    p->position = F3(-0.9f, 32.9f, 32.9f);
    tick(w);
    CHECK(!p->block_changed && !p->offset_changed);
    tw_destroy(w);
}

/* ---- no tunnelling into terrain --------------------------------------- */

TEST(entities_never_end_up_inside_solid_blocks) {
    test_rng_seed(99);
    struct World *w = flat_world();
    /* rough heightfield with steps of up to 3 blocks */
    for (int x = -12; x < 12; x++) for (int z = -12; z < 12; z++) {
        int h = (int)test_rng_range(0, 3);
        fill_box(w, V3(x, 0, z), V3(x, h, z), test_rng_range(0, 1) ? STONE : DIRT);
    }
    fill_box(w, V3(-13, 0, -13), V3(12, 30, -13), STONE);   /* fences */
    fill_box(w, V3(-13, 0, 12), V3(12, 30, 12), STONE);
    fill_box(w, V3(-13, 0, -13), V3(-13, 30, 12), STONE);
    fill_box(w, V3(12, 0, -13), V3(12, 30, 12), STONE);

    struct Entity es[12];
    vec3s push[12];
    for (int i = 0; i < 12; i++) {
        vec3s size = i % 2 ? PLAYER_SIZE : F3(0.6f, 1.8f, 0.6f);
        es[i] = spawn(w, F3(test_rng_range(-900, 900) / 100.0f, 9.0f + i, test_rng_range(-900, 900) / 100.0f),
                      size, true, true);
        push[i] = F3(test_rng_range(-12, 12) / 100.0f, 0, test_rng_range(-12, 12) / 100.0f);
    }
    for (int t = 0; t < 400; t++) {
        for (int i = 0; i < 12; i++) {
            phys(es[i])->velocity.x = push[i].x;
            phys(es[i])->velocity.z = push[i].z;
            if (t % 60 == 0) phys(es[i])->velocity.y = 0.2f; /* hop now and then */
        }
        tick(w);
        for (int i = 0; i < 12; i++)
            CHECK_MSG(max_penetration(w, es[i]) <= OVERLAP_TOL,
                      "tick %d: entity %d at (%f,%f,%f) inside terrain (depth %f)", t, i,
                      pos_of(es[i]).x, pos_of(es[i]).y, pos_of(es[i]).z, max_penetration(w, es[i]));
    }
    /* nobody fell through the ground */
    for (int i = 0; i < 12; i++)
        CHECK_MSG(pos_of(es[i]).y > 1.0f, "entity %d fell to (%f,%f,%f)", i,
                  pos_of(es[i]).x, pos_of(es[i]).y, pos_of(es[i]).z);
    tw_destroy(w);
}

/* ---- movement component (player controls) ------------------------------ */

static struct Entity spawn_player(struct World *w, vec3s pos) {
    struct Entity e = spawn(w, pos, PLAYER_SIZE, true, true);
    ecs_add(e, C_MOVEMENT, ((struct MovementComponent) { .speed = 1.0f, .jump_height = 1.0f }));
    ecs_add(e, C_CAMERA, ((struct CameraComponent) { .offset = F3(0, 0.8f, 0) }));
    return e;
}
static struct MovementComponent *mov(struct Entity e) { return ecs_get(e, C_MOVEMENT); }

TEST(walking_forward_moves_along_view_direction) {
    struct World *w = flat_world();
    fill_box(w, V3(-10, 0, -10), V3(10, 0, 10), STONE);
    struct Entity e = spawn_player(w, F3(0.5f, 1.81f, 0.5f));
    for (int t = 0; t < 20; t++) tick(w);   /* settle on the ground */
    vec3s start = pos_of(e);
    mov(e)->directions.forward = true;      /* yaw 0 looks along +z */
    for (int t = 0; t < 60; t++) tick(w);
    vec3s end = pos_of(e);
    CHECK_MSG(end.z - start.z > 1.0f, "walked only %f", end.z - start.z);
    CHECK_NEAR(end.x, start.x, 0.05);
    CHECK_NEAR(end.y, start.y, 0.05);
    /* stopping: no more input, entity comes to rest quickly */
    mov(e)->directions.forward = false;
    for (int t = 0; t < 40; t++) tick(w);
    vec3s a = pos_of(e);
    tick(w);
    CHECK_NEAR(pos_of(e).z, a.z, 1e-3);
    tw_destroy(w);
}

TEST(jump_from_ground_rises_and_lands) {
    struct World *w = flat_world();
    fill_box(w, V3(-3, 0, -3), V3(3, 0, 3), STONE);
    struct Entity e = spawn_player(w, F3(0.5f, 1.81f, 0.5f));
    for (int t = 0; t < 20; t++) tick(w);
    CHECK(phys(e)->flags.grounded);
    float ground_y = pos_of(e).y, peak = ground_y;
    mov(e)->directions.up = true;
    tick(w);
    mov(e)->directions.up = false;
    for (int t = 0; t < 120; t++) {
        tick(w);
        if (pos_of(e).y > peak) peak = pos_of(e).y;
    }
    CHECK_MSG(peak > ground_y + 0.5f, "jump peak only %f above ground", peak - ground_y);
    CHECK_NEAR(pos_of(e).y, ground_y, REST_TOL);
    CHECK(phys(e)->flags.grounded);
    tw_destroy(w);
}

TEST(cannot_jump_in_mid_air) {
    struct World *w = flat_world();
    struct Entity e = spawn_player(w, F3(0.5f, 40.0f, 0.5f));
    for (int t = 0; t < 5; t++) tick(w);
    float vy = phys(e)->velocity.y;
    mov(e)->directions.up = true;
    tick(w);
    CHECK(phys(e)->velocity.y <= vy);
    tw_destroy(w);
}

TEST(flying_ignores_gravity_and_moves_vertically) {
    struct World *w = flat_world();
    struct Entity e = spawn_player(w, F3(0.5f, 20.0f, 0.5f));
    mov(e)->flags.flying = true;
    tick(w);   /* (gravity may still act during the tick that switches flying on) */
    float y = pos_of(e).y;
    CHECK_NEAR(y, 20.0f, 0.05);
    for (int t = 0; t < 30; t++) tick(w);
    CHECK_NEAR(pos_of(e).y, y, 1e-4);
    mov(e)->directions.up = true;
    for (int t = 0; t < 10; t++) tick(w);
    CHECK(pos_of(e).y > y + 0.5f);
    tw_destroy(w);
}

TEST(swimming_up_in_water_rises) {
    struct World *w = flat_world();
    fill_box(w, V3(-3, 0, -3), V3(3, 0, 3), STONE);
    fill_box(w, V3(-3, 1, -3), V3(3, 10, 3), WATER);
    struct Entity e = spawn_player(w, F3(0.5f, 3.0f, 0.5f));
    for (int t = 0; t < 400; t++) tick(w);   /* sink to the bottom */
    float bottom_y = pos_of(e).y;
    CHECK(bottom_y < 3.0f);
    mov(e)->directions.up = true;
    for (int t = 0; t < 60; t++) tick(w);
    CHECK_MSG(pos_of(e).y > bottom_y + 1.0f, "rose only %f", pos_of(e).y - bottom_y);
    tw_destroy(w);
}


KNOWN_DEFECT(flying_player_does_not_clip_into_walls,
             "physics_move only collects colliders inside the entity box scaled x2 around the "
             "START position (0.1 block beyond a 0.2-wide player in x/z), not along the swept "
             "path; a move longer than that towards -x/-z (or a large -y move) ends inside a "
             "solid block. A flying player (0.35 blocks/tick) clips ~0.15 into a wall before "
             "being pushed out; physics_move(0,-8.6,0) above a floor lands inside it.") {
    struct World *w = flat_world();
    fill_box(w, V3(-10, 0, -10), V3(10, 0, 10), STONE);
    fill_box(w, V3(-4, 1, -10), V3(-4, 6, 10), STONE);  /* wall, +x face at x=-3 */
    struct Entity e = spawn_player(w, F3(3.3f, 3.0f, 0.5f));
    mov(e)->flags.flying = true;
    mov(e)->directions.right = true;  /* with yaw 0 this is towards -x */
    for (int t = 0; t < 40; t++) {
        tick(w);
        CHECK_MSG(max_penetration(w, e) <= OVERLAP_TOL, "tick %d: flying player %f deep inside the wall",
                  t, max_penetration(w, e));
    }
    CHECK(pos_of(e).x < -2.0f);   /* it did reach the wall */
    tw_destroy(w);
}

/* Gameplay contract for jumping (added after a mutation probe showed that
 * halving gravity went unnoticed): a standing jump lifts the player high
 * enough to step onto a one-block ledge, but not over a two-block wall. */
TEST(jump_clears_one_block_but_not_two) {
    struct World *w = flat_world();
    fill_box(w, V3(-3, 0, -3), V3(3, 0, 3), STONE);
    struct Entity e = spawn_player(w, F3(0.5f, 1.81f, 0.5f));
    for (int t = 0; t < 20; t++) tick(w);
    float feet = pos_of(e).y - PLAYER_SIZE.y / 2, peak = feet;
    int airborne_ticks = 0;
    mov(e)->directions.up = true;
    tick(w);
    mov(e)->directions.up = false;
    for (int t = 0; t < 120; t++) {
        tick(w);
        float f = pos_of(e).y - PLAYER_SIZE.y / 2;
        if (f > peak) peak = f;
        if (f > feet + 0.01f) airborne_ticks = t + 2;
    }
    CHECK_MSG(peak - feet > 1.0f, "jump apex %f blocks: cannot reach a one-block ledge", peak - feet);
    CHECK_MSG(peak - feet < 2.0f, "jump apex %f blocks: clears a two-block wall", peak - feet);
    /* feel: a jump lasts roughly half a second at TICKRATE 60 (currently ~33
       ticks); a broad band so tuning-neutral refactors pass but changed
       gravity or jump impulse is noticed */
    CHECK_MSG(airborne_ticks >= 24 && airborne_ticks <= 42, "jump lasted %d ticks", airborne_ticks);
    tw_destroy(w);
}

TEST(jumping_while_walking_climbs_a_one_block_step) {
    struct World *w = flat_world();
    fill_box(w, V3(-3, 0, -3), V3(3, 0, 8), STONE);
    fill_box(w, V3(-3, 1, 3), V3(3, 1, 8), STONE); /* one-block step starting at z = 3 */
    struct Entity e = spawn_player(w, F3(0.5f, 1.81f, 0.5f));
    for (int t = 0; t < 20; t++) tick(w);
    mov(e)->directions.forward = true;
    for (int t = 0; t < 120; t++) {
        mov(e)->directions.up = (t % 10) == 0;
        tick(w);
    }
    mov(e)->directions.forward = false;
    mov(e)->directions.up = false;
    for (int t = 0; t < 40; t++) tick(w);
    CHECK_MSG(pos_of(e).z > 3.5f, "player did not get onto the step (z=%f)", pos_of(e).z);
    CHECK_NEAR(pos_of(e).y - PLAYER_SIZE.y / 2, 2.0f, REST_TOL);
    tw_destroy(w);
}

int main(int argc, char **argv) { return run_tests(argc, argv); }
