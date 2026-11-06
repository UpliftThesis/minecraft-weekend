#include "shim.h"
#include "../../src/entity/ecs.h"
#include <stdio.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

struct Value { int value; };
static int init_calls, destroy_calls, tick_calls, tick_sum;
static EntityId last_entity;
static void on_init(void *ptr, struct Entity e) {
    struct Value *v = ptr;
    ++init_calls;
    last_entity = e.id;
    v->value += 10;
}
static void on_destroy(void *ptr, struct Entity e) {
    (void)ptr;
    ++destroy_calls;
    last_entity = e.id;
}
static void on_tick(void *ptr, struct Entity e) {
    struct Value *v = ptr;
    ++tick_calls;
    tick_sum += v->value;
    last_entity = e.id;
}
void _ecs_init_internal(struct ECS *ecs) {
    union ECSSystem value_system = { .init = on_init, .destroy = on_destroy, .tick = on_tick };
    union ECSSystem other_system = {0};
    ecs_register(TEST_VALUE, struct Value, ecs, value_system);
    ecs_register(TEST_OTHER, struct Value, ecs, other_system);
}

static void lifecycle(void) {
    struct ECS ecs = {0};
    ecs_init(&ecs, NULL);
    struct Entity first = ecs_new(&ecs), second = ecs_new(&ecs);
    CHECK(first.id != second.id && first.id != ENTITY_NONE);
    CHECK(!ecs_has(first, TEST_VALUE));
    struct Value value = {32};
    _ecs_add_internal(first, TEST_VALUE, &value);
    CHECK(ecs_has(first, TEST_VALUE));
    CHECK(!ecs_has(second, TEST_VALUE));
    CHECK(init_calls == 1 && last_entity == first.id);
    CHECK(((struct Value *)ecs_get(first, TEST_VALUE))->value == 42);
    ecs_event(&ecs, ECS_TICK);
    CHECK(tick_calls == 1 && tick_sum == 42 && last_entity == first.id);
    ecs_remove(first, TEST_VALUE);
    CHECK(!ecs_has(first, TEST_VALUE));
    CHECK(destroy_calls == 1 && last_entity == first.id);
    ecs_event(&ecs, ECS_TICK);
    CHECK(tick_calls == 1);
    _ecs_add_internal(first, TEST_VALUE, &value);
    ecs_delete(&ecs, first);
    CHECK(destroy_calls == 2 && last_entity == first.id);
    struct Entity replacement = ecs_new(&ecs);
    CHECK(replacement.id != first.id && replacement.id != second.id);
    CHECK(!ecs_has(replacement, TEST_VALUE));
    ecs_destroy(&ecs);
}

static void growth_preserves_entities(void) {
    enum { N = 140 };
    struct ECS ecs = {0};
    struct Entity entities[N];
    ecs_init(&ecs, NULL);
    for (int i = 0; i < N; ++i) {
        entities[i] = ecs_new(&ecs);
        struct Value value = {i};
        _ecs_add_internal(entities[i], TEST_OTHER, &value);
    }
    for (int i = 0; i < N; ++i) {
        CHECK(entities[i].id != ENTITY_NONE);
        for (int j = 0; j < i; ++j) CHECK(entities[i].id != entities[j].id);
        CHECK(ecs_has(entities[i], TEST_OTHER));
        CHECK(((struct Value *)ecs_get(entities[i], TEST_OTHER))->value == i);
    }
    ecs_destroy(&ecs);
}
int main(void) {
    lifecycle();
    growth_preserves_entities();
    puts("ECS lifecycle regression tests passed");
    return 0;
}
