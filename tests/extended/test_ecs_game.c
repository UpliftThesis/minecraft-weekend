/* Regression tests for the entity component system (src/entity/ecs.c) as the
 * game uses it: with the production component set registered by ecs_init on a
 * headless world. tests/ecs_lifecycle covers a toy component set; these tests
 * cover slot/id management, deletion, event dispatch over all production
 * component types, storage growth and alignment.
 *
 * Note: under SANITIZE=1, UBSan reports misaligned ECSTag accesses inside
 * ecs.c; that is an already documented defect (see test_known_defects.c). */
#include <stdalign.h>
#include "check.h"
#include "harness.h"

#define V3(x, y, z) ((ivec3s){{ (x), (y), (z) }})
#define F3(x, y, z) ((vec3s){{ (x), (y), (z) }})

#define NUM_COMPONENTS (ECSCOMPONENT_LAST + 1)

/* ---- helpers --------------------------------------------------------- */

static struct World *empty_world(void) { return tw_create(V3(0, 0, 0), 3, 1); }

static struct Entity with_position(struct World *w, vec3s p) {
    struct Entity e = ecs_new(&w->ecs);
    ecs_add(e, C_POSITION, ((struct PositionComponent) { .position = p }));
    return e;
}

static struct PositionComponent *position(struct Entity e) { return ecs_get(e, C_POSITION); }

/* Hooks that record which (component type, entity slot) got which event.
 * Installed with ecs_register (the registration API) in place of the
 * production systems for the dispatch tests. */
#define MAX_SLOTS 300
static int calls[NUM_COMPONENTS][ECSEVENT_LAST + 1][MAX_SLOTS];
static EntityId last_id[NUM_COMPONENTS][MAX_SLOTS];

#define DECL_HOOKS(c)                                                                   \
    static void hook_##c##_0(void *p, struct Entity e) { calls[c][0][e.index]++; last_id[c][e.index] = e.id; } \
    static void hook_##c##_1(void *p, struct Entity e) { calls[c][1][e.index]++; last_id[c][e.index] = e.id; } \
    static void hook_##c##_2(void *p, struct Entity e) { calls[c][2][e.index]++; last_id[c][e.index] = e.id; } \
    static void hook_##c##_3(void *p, struct Entity e) { calls[c][3][e.index]++; last_id[c][e.index] = e.id; } \
    static void hook_##c##_4(void *p, struct Entity e) { calls[c][4][e.index]++; last_id[c][e.index] = e.id; } \
    static const union ECSSystem sys_##c = { .subscribers = { hook_##c##_0, hook_##c##_1, hook_##c##_2, hook_##c##_3, hook_##c##_4 } };
DECL_HOOKS(0) DECL_HOOKS(1) DECL_HOOKS(2) DECL_HOOKS(3)
DECL_HOOKS(4) DECL_HOOKS(5) DECL_HOOKS(6) DECL_HOOKS(7)
_Static_assert(NUM_COMPONENTS == 8, "update the hook table for new component types");

/* re-register every production component type (same storage types) with recording hooks */
static void install_recording_systems(struct ECS *ecs) {
    ecs_register(C_POSITION, struct PositionComponent, ecs, sys_0);
    ecs_register(C_CAMERA, struct CameraComponent, ecs, sys_1);
    ecs_register(C_CONTROL, struct ControlComponent, ecs, sys_2);
    ecs_register(C_PHYSICS, struct PhysicsComponent, ecs, sys_3);
    ecs_register(C_MOVEMENT, struct MovementComponent, ecs, sys_4);
    ecs_register(C_BLOCKLOOK, struct BlockLookComponent, ecs, sys_5);
    ecs_register(C_DEBUG, struct DebugComponent, ecs, sys_6);
    ecs_register(C_LIGHT, struct LightComponent, ecs, sys_7);
}

/* ---- ids and slots ---------------------------------------------------- */

TEST(ids_unique_and_never_reused_slots_reused) {
    struct World *w = empty_world();
    struct Entity es[10];
    EntityId seen[64];
    size_t nseen = 0;
    for (int i = 0; i < 10; i++) {
        es[i] = ecs_new(&w->ecs);
        CHECK(es[i].id != ENTITY_NONE);
        seen[nseen++] = es[i].id;
    }
    /* free three slots */
    u64 freed[3] = { es[2].index, es[5].index, es[9].index };
    ecs_delete(&w->ecs, es[2]);
    ecs_delete(&w->ecs, es[5]);
    ecs_delete(&w->ecs, es[9]);
    for (int k = 0; k < 3; k++) {
        struct Entity n = ecs_new(&w->ecs);
        CHECK_MSG(n.index == freed[0] || n.index == freed[1] || n.index == freed[2],
                  "new entity took slot %llu instead of a freed one", (unsigned long long)n.index);
        for (size_t j = 0; j < nseen; j++) CHECK_MSG(n.id != seen[j], "id %llu reused", (unsigned long long)n.id);
        CHECK(n.id != ENTITY_NONE);
        seen[nseen++] = n.id;
    }
    /* slots of live entities are distinct */
    tw_destroy(w);
}

TEST(ids_unique_over_many_create_delete_cycles) {
    struct World *w = empty_world();
    test_rng_seed(5);
    struct Entity live[40];
    int nlive = 0;
    static EntityId seen[5000];
    size_t nseen = 0;
    for (int step = 0; step < 2000; step++) {
        if (nlive < 40 && (nlive == 0 || test_rng_range(0, 1))) {
            struct Entity e = ecs_new(&w->ecs);
            for (int i = 0; i < nlive; i++) CHECK(live[i].index != e.index);
            seen[nseen++] = e.id;
            live[nlive++] = e;
        } else {
            int k = (int)test_rng_range(0, nlive - 1);
            ecs_delete(&w->ecs, live[k]);
            live[k] = live[--nlive];
        }
    }
    for (size_t i = 0; i < nseen; i++)
        for (size_t j = 0; j < i; j++) CHECK(seen[i] != seen[j]);
    tw_destroy(w);
}

/* ---- deletion --------------------------------------------------------- */

TEST(delete_removes_all_components) {
    struct World *w = empty_world();
    struct Entity e = with_position(w, F3(1, 2, 3));
    ecs_add(e, C_PHYSICS, ((struct PhysicsComponent) { .size = { F3(0, 0, 0), F3(1, 1, 1) } }));
    ecs_add(e, C_MOVEMENT, ((struct MovementComponent) { .speed = 1 }));
    ecs_add(e, C_DEBUG, ((struct DebugComponent) { .enabled = true }));
    struct Entity other = with_position(w, F3(9, 9, 9));
    ecs_add(other, C_DEBUG, ((struct DebugComponent) { .enabled = true }));

    ecs_delete(&w->ecs, e);
    struct Entity n = ecs_new(&w->ecs);
    CHECK_EQ(n.index, e.index);
    for (int c = 0; c <= ECSCOMPONENT_LAST; c++)
        CHECK_MSG(!ecs_has(n, (enum ECSComponent)c), "component %d survived deletion", c);
    /* the other entity is unaffected */
    CHECK(ecs_has(other, C_POSITION) && ecs_has(other, C_DEBUG));
    CHECK_NEAR(position(other)->position.x, 9, 0);
    /* deleted entity receives no more events: only `other` moves on tick */
    ecs_add(n, C_DEBUG);
    ecs_event(&w->ecs, ECS_TICK);
    CHECK_IVEC3_EQ(position(other)->block, V3(9, 9, 9));
    tw_destroy(w);
}

TEST(delete_runs_destroy_hook_of_owned_components) {
    struct World *w = empty_world();
    install_recording_systems(&w->ecs);
    memset(calls, 0, sizeof calls);
    struct Entity e = ecs_new(&w->ecs);
    ecs_add(e, C_POSITION);
    ecs_add(e, C_PHYSICS);
    ecs_add(e, C_LIGHT);
    CHECK_EQ(calls[C_POSITION][ECS_INIT][e.index], 1);
    CHECK_EQ(calls[C_PHYSICS][ECS_INIT][e.index], 1);
    CHECK_EQ(calls[C_LIGHT][ECS_INIT][e.index], 1);
    ecs_delete(&w->ecs, e);
    CHECK_EQ(calls[C_POSITION][ECS_DESTROY][e.index], 1);
    CHECK_EQ(calls[C_PHYSICS][ECS_DESTROY][e.index], 1);
    CHECK_EQ(calls[C_LIGHT][ECS_DESTROY][e.index], 1);
    CHECK(last_id[C_PHYSICS][e.index] == e.id);
    /* nothing remains to receive events */
    ecs_event(&w->ecs, ECS_TICK);
    for (int c = 0; c <= ECSCOMPONENT_LAST; c++) CHECK_EQ(calls[c][ECS_TICK][e.index], 0);
    tw_destroy(w);
}

KNOWN_DEFECT(destroy_hook_runs_only_for_components_the_entity_has,
             "ecs_delete calls the destroy hook of EVERY component type for the deleted slot, "
             "including components the entity never had or that ecs_remove already destroyed "
             "(double destroy); any hook that frees resources would act on stale/zero data") {
    struct World *w = empty_world();
    install_recording_systems(&w->ecs);
    memset(calls, 0, sizeof calls);
    struct Entity e = ecs_new(&w->ecs);
    ecs_add(e, C_POSITION);
    ecs_add(e, C_DEBUG);
    ecs_remove(e, C_DEBUG);
    CHECK_EQ(calls[C_DEBUG][ECS_DESTROY][e.index], 1);
    ecs_delete(&w->ecs, e);
    CHECK_EQ(calls[C_POSITION][ECS_DESTROY][e.index], 1);
    CHECK_MSG(calls[C_DEBUG][ECS_DESTROY][e.index] == 1, "removed component destroyed %d times",
              calls[C_DEBUG][ECS_DESTROY][e.index]);
    CHECK_MSG(calls[C_CAMERA][ECS_DESTROY][e.index] == 0, "never-added component destroyed %d times",
              calls[C_CAMERA][ECS_DESTROY][e.index]);
    tw_destroy(w);
}

KNOWN_DEFECT(component_added_without_value_starts_zeroed_in_reused_slot,
             "ecs_add(entity, C) without a value keeps whatever the previous occupant of the slot "
             "left there: ecs_delete/ecs_remove never clear component storage, so a fresh entity "
             "inherits e.g. the old position/velocity (main.c relies on zeroed defaults for "
             "C_POSITION, C_CONTROL and C_LIGHT)") {
    struct World *w = empty_world();
    /* a fresh slot starts zeroed */
    struct Entity a = ecs_new(&w->ecs);
    ecs_add(a, C_POSITION);
    CHECK_NEAR(position(a)->position.y, 0, 0);
    position(a)->position = F3(7, 8, 9);
    ecs_add(a, C_PHYSICS, ((struct PhysicsComponent) { .velocity = F3(1, 2, 3) }));
    ecs_delete(&w->ecs, a);

    struct Entity b = ecs_new(&w->ecs);
    CHECK_EQ(b.index, a.index);
    ecs_add(b, C_POSITION);
    ecs_add(b, C_PHYSICS);
    CHECK_MSG(position(b)->position.x == 0 && position(b)->position.y == 0,
              "new entity starts at the old entity's position (%g,%g,%g)",
              position(b)->position.x, position(b)->position.y, position(b)->position.z);
    struct PhysicsComponent *p = ecs_get(b, C_PHYSICS);
    CHECK(p->velocity.x == 0 && p->velocity.y == 0 && p->velocity.z == 0);
    tw_destroy(w);
}

TEST(remove_single_component_keeps_the_others) {
    struct World *w = empty_world();
    struct Entity e = with_position(w, F3(4.5f, 4.5f, 4.5f));
    ecs_add(e, C_DEBUG, ((struct DebugComponent) { .enabled = true }));
    ecs_add(e, C_MOVEMENT, ((struct MovementComponent) { .speed = 3.5f }));
    ecs_remove(e, C_DEBUG);
    CHECK(!ecs_has(e, C_DEBUG));
    CHECK(ecs_has(e, C_POSITION) && ecs_has(e, C_MOVEMENT));
    CHECK_NEAR(((struct MovementComponent *)ecs_get(e, C_MOVEMENT))->speed, 3.5f, 0);
    CHECK_NEAR(position(e)->position.x, 4.5f, 0);
    /* can be added again */
    ecs_add(e, C_DEBUG, ((struct DebugComponent) { .enabled = false }));
    CHECK(ecs_has(e, C_DEBUG));
    tw_destroy(w);
}

/* ---- event dispatch --------------------------------------------------- */

TEST(events_reach_every_component_type_of_every_entity) {
    struct World *w = empty_world();
    install_recording_systems(&w->ecs);
    memset(calls, 0, sizeof calls);
    /* 150 entities (forces storage growth); entity i owns component c iff bit c of (i*37+11) */
    enum { N = 150 };
    struct Entity es[N];
    for (int i = 0; i < N; i++) {
        es[i] = ecs_new(&w->ecs);
        CHECK(es[i].index < MAX_SLOTS);
        unsigned mask = (unsigned)(i * 37 + 11) & 0xFF;
        for (int c = 0; c <= ECSCOMPONENT_LAST; c++)
            if (mask & (1u << c)) ecs_add(es[i], (enum ECSComponent)c);
    }
    for (int ev = ECS_RENDER; ev <= ECSEVENT_LAST; ev++) {
        ecs_event(&w->ecs, (enum ECSEvent)ev);
        ecs_event(&w->ecs, (enum ECSEvent)ev);
    }
    for (int i = 0; i < N; i++) {
        unsigned mask = (unsigned)(i * 37 + 11) & 0xFF;
        for (int c = 0; c <= ECSCOMPONENT_LAST; c++) {
            int expect = (mask & (1u << c)) ? 1 : 0;
            CHECK_EQ(calls[c][ECS_INIT][es[i].index], expect);
            for (int ev = ECS_RENDER; ev <= ECSEVENT_LAST; ev++)
                CHECK_MSG(calls[c][ev][es[i].index] == 2 * expect,
                          "entity %d component %d event %d: %d calls", i, c, ev, calls[c][ev][es[i].index]);
            if (expect) CHECK(last_id[c][es[i].index] == es[i].id);
            CHECK_EQ(calls[c][ECS_DESTROY][es[i].index], 0);
        }
    }
    tw_destroy(w);
}

TEST(tick_drives_production_components) {
    struct World *w = empty_world();
    tw_fill_empty(w);
    /* position component: block coordinates follow the position on tick */
    struct Entity a = with_position(w, F3(5.5f, 6.5f, -7.5f));
    /* physics component: gravity changes velocity and position */
    struct Entity b = with_position(w, F3(0.5f, 20.0f, 0.5f));
    ecs_add(b, C_PHYSICS, ((struct PhysicsComponent) {
        .size = { F3(0, 0, 0), F3(0.2f, 1.6f, 0.2f) }, .flags = { .gravity = true, .collide = true } }));
    /* light component: emits light at the entity's block */
    struct Entity c = with_position(w, F3(10.5f, 10.5f, 10.5f));
    ecs_add(c, C_LIGHT, ((struct LightComponent) { .light = 0xFFFF, .flags = { .enabled = true } }));
    CHECK_EQ(world_get_torchlight(w, V3(10, 10, 10)), 0);

    ecs_event(&w->ecs, ECS_TICK);
    ecs_event(&w->ecs, ECS_TICK);

    CHECK_IVEC3_EQ(position(a)->block, V3(5, 6, -8));
    CHECK(position(b)->position.y < 20.0f);
    CHECK(((struct PhysicsComponent *)ecs_get(b, C_PHYSICS))->velocity.y < 0);
    CHECK(world_get_torchlight(w, V3(10, 10, 10)) != 0);
    tw_destroy(w);
}

TEST(update_and_init_drive_camera_component) {
    struct World *w = empty_world();
    struct Entity e = with_position(w, F3(1, 2, 3));
    ecs_add(e, C_CAMERA, ((struct CameraComponent) { .offset = F3(0, 0.8f, 0) }));
    struct CameraComponent *cam = ecs_get(e, C_CAMERA);
    /* init places the camera at position + offset */
    CHECK_NEAR(cam->camera.position.y, 2.8f, 1e-5);
    position(e)->position = F3(-4, 10, 6);
    ecs_event(&w->ecs, ECS_UPDATE);
    CHECK_NEAR(cam->camera.position.x, -4, 1e-5);
    CHECK_NEAR(cam->camera.position.y, 10.8f, 1e-5);
    CHECK_NEAR(cam->camera.position.z, 6, 1e-5);
    tw_destroy(w);
}

/* ---- storage growth --------------------------------------------------- */

TEST(growth_preserves_production_component_data) {
    struct World *w = empty_world();
    enum { N = 300 };
    static struct Entity es[N];
    for (int i = 0; i < N; i++) {
        es[i] = with_position(w, F3(i, -i, 0.5f * i));
        if (i % 2 == 0)
            ecs_add(es[i], C_PHYSICS, ((struct PhysicsComponent) {
                .velocity = F3(i, 1, 2), .size = { F3(0, 0, 0), F3(1, 2, 3) }, .flags = { .collide = true } }));
        if (i % 3 == 0)
            ecs_add(es[i], C_MOVEMENT, ((struct MovementComponent) { .speed = (f32)i, .jump_height = 2 }));
        if (i % 5 == 0)
            ecs_add(es[i], C_BLOCKLOOK, ((struct BlockLookComponent) { .pos = V3(i, i, i), .radius = 5 }));
        /* re-check early entities while capacity grows past 64 / 128 / 256 */
        if (i == 64 || i == 128 || i == 256) {
            for (int j = 0; j < i; j++) {
                CHECK_MSG(ecs_has(es[j], C_POSITION), "after %d entities: entity %d lost its position", i, j);
                CHECK_NEAR(position(es[j])->position.x, j, 0);
            }
        }
    }
    /* delete some, so the survivors must keep their data in place */
    for (int i = 0; i < N; i += 7) ecs_delete(&w->ecs, es[i]);
    for (int i = 0; i < 50; i++) with_position(w, F3(-1, -1, -1));

    for (int i = 0; i < N; i++) {
        if (i % 7 == 0) continue;
        CHECK_MSG(ecs_has(es[i], C_POSITION), "entity %d (slot %llu) lost its position", i, (unsigned long long)es[i].index);
        struct PositionComponent *p = position(es[i]);
        CHECK_MSG(p->position.x == i && p->position.y == -i && p->position.z == 0.5f * i,
                  "entity %d position corrupted", i);
        CHECK_EQ(ecs_has(es[i], C_PHYSICS), i % 2 == 0);
        CHECK_EQ(ecs_has(es[i], C_MOVEMENT), i % 3 == 0);
        CHECK_EQ(ecs_has(es[i], C_BLOCKLOOK), i % 5 == 0);
        CHECK(!ecs_has(es[i], C_CAMERA) && !ecs_has(es[i], C_LIGHT));
        if (i % 2 == 0) {
            struct PhysicsComponent *ph = ecs_get(es[i], C_PHYSICS);
            CHECK(ph->velocity.x == i && ph->size[1].z == 3 && ph->flags.collide);
        }
        if (i % 3 == 0) CHECK(((struct MovementComponent *)ecs_get(es[i], C_MOVEMENT))->speed == i);
        if (i % 5 == 0) CHECK_IVEC3_EQ(((struct BlockLookComponent *)ecs_get(es[i], C_BLOCKLOOK))->pos, V3(i, i, i));
    }
    /* every live entity still gets ticked after growth (movement / block-look
     * ticks need a camera, so drop those components first) */
    for (int i = 0; i < N; i++) {
        if (i % 7 == 0) continue;
        if (ecs_has(es[i], C_MOVEMENT)) ecs_remove(es[i], C_MOVEMENT);
        if (ecs_has(es[i], C_BLOCKLOOK)) ecs_remove(es[i], C_BLOCKLOOK);
    }
    static vec3s before[N];
    for (int i = 1; i < N; i++) if (i % 7) before[i] = position(es[i])->position;
    ecs_event(&w->ecs, ECS_TICK);
    for (int i = 1; i < N; i++) {
        if (i % 7 == 0) continue;
        /* block tracks the position (from before or after this tick's physics move) */
        ivec3s blk = position(es[i])->block, b0 = world_pos_to_block(before[i]),
               b1 = world_pos_to_block(position(es[i])->position);
        CHECK(!ivec3scmp(blk, b0) || !ivec3scmp(blk, b1));
        if (i % 2 == 0) CHECK(position(es[i])->position.x != before[i].x);
        else CHECK(position(es[i])->position.x == before[i].x);
    }
    tw_destroy(w);
}

/* ---- alignment -------------------------------------------------------- */

#define CHECK_ALIGNED(e, C, T)                                                         \
    do {                                                                               \
        uintptr_t p_ = (uintptr_t)ecs_get((e), (C));                                    \
        CHECK_MSG(p_ % alignof(T) == 0, #T " of slot %llu at %p not %zu-aligned",       \
                  (unsigned long long)(e).index, (void *)p_, alignof(T));               \
    } while (0)

TEST(component_storage_is_aligned_for_component_types) {
    struct World *w = empty_world();
    for (int i = 0; i < 140; i++) {
        struct Entity e = with_position(w, F3(i, 0, 0));
        ecs_add(e, C_CAMERA, ((struct CameraComponent) { .offset = F3(0, 1, 0) }));
        ecs_add(e, C_CONTROL, ((struct ControlComponent) { .mouse_sensitivity = 1 }));
        ecs_add(e, C_PHYSICS, ((struct PhysicsComponent) { .size = { F3(0, 0, 0), F3(1, 1, 1) } }));
        ecs_add(e, C_MOVEMENT, ((struct MovementComponent) { .speed = 1 }));
        ecs_add(e, C_BLOCKLOOK, ((struct BlockLookComponent) { .radius = 1 }));
        ecs_add(e, C_DEBUG, ((struct DebugComponent) { .enabled = true }));
        ecs_add(e, C_LIGHT, ((struct LightComponent) { .light = 1 }));
        CHECK_ALIGNED(e, C_POSITION, struct PositionComponent);
        CHECK_ALIGNED(e, C_CAMERA, struct CameraComponent);
        CHECK_ALIGNED(e, C_CONTROL, struct ControlComponent);
        CHECK_ALIGNED(e, C_PHYSICS, struct PhysicsComponent);
        CHECK_ALIGNED(e, C_MOVEMENT, struct MovementComponent);
        CHECK_ALIGNED(e, C_BLOCKLOOK, struct BlockLookComponent);
        CHECK_ALIGNED(e, C_DEBUG, struct DebugComponent);
        CHECK_ALIGNED(e, C_LIGHT, struct LightComponent);
    }
    tw_destroy(w);
}

int main(int argc, char **argv) { return run_tests(argc, argv); }
