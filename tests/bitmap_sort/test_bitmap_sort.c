#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../src/util/bitmap.h"
#include "../../src/util/sort.h"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
    exit(1); \
} } while (0)

static void test_bitmap_zero_and_boundaries(void) {
    const size_t bits = 137;
    const size_t selected[] = {0, 1, 7, 8, 15, 16, 63, 64, 65, 127, 128, 136};
    Bitmap map = bitmap_calloc(bits);
    CHECK(map != NULL);
    for (size_t i = 0; i < bits; ++i) CHECK(!bitmap_get(map, i));
    for (size_t i = 0; i < sizeof selected / sizeof *selected; ++i) {
        bitmap_set(map, selected[i]);
        bitmap_set(map, selected[i]);
    }
    for (size_t i = 0; i < bits; ++i) {
        bool expected = false;
        for (size_t j = 0; j < sizeof selected / sizeof *selected; ++j)
            if (i == selected[j]) expected = true;
        CHECK(bitmap_get(map, i) == expected);
    }
    for (size_t i = 0; i < sizeof selected / sizeof *selected; i += 2) {
        bitmap_clr(map, selected[i]);
        bitmap_clr(map, selected[i]);
    }
    for (size_t i = 0; i < sizeof selected / sizeof *selected; ++i)
        CHECK(bitmap_get(map, selected[i]) == (i % 2 == 1));
    free(map);
}

static void test_bitmap_alloc_and_resize(void) {
    Bitmap map = bitmap_alloc(70);
    CHECK(map != NULL);
    for (size_t i = 0; i < 70; ++i) bitmap_clr(map, i);
    bitmap_set(map, 0);
    bitmap_set(map, 63);
    bitmap_set(map, 69);
    Bitmap grown = bitmap_realloc(map, 200);
    CHECK(grown != NULL);
    map = grown;
    for (size_t i = 0; i < 70; ++i)
        CHECK(bitmap_get(map, i) == (i == 0 || i == 63 || i == 69));
    for (size_t i = 70; i < 200; ++i) bitmap_clr(map, i);
    bitmap_set(map, 199);
    CHECK(bitmap_get(map, 199));
    Bitmap shrunk = bitmap_realloc(map, 32);
    CHECK(shrunk != NULL);
    map = shrunk;
    for (size_t i = 0; i < 32; ++i) CHECK(bitmap_get(map, i) == (i == 0));
    free(map);
}

struct item { int key; int id; };
struct order { int direction; int calls; };
static int compare_items(const void *a, const void *b, void *context) {
    const struct item *x = a, *y = b;
    struct order *order = context;
    ++order->calls;
    int cmp = (x->key > y->key) - (x->key < y->key);
    if (!cmp) cmp = (x->id > y->id) - (x->id < y->id);
    return order->direction * cmp;
}

static void test_sort(void) {
    struct item items[] = {{3, 7}, {-1, 4}, {3, 2}, {0, 9}, {-1, 1}};
    const struct item ascending[] = {{-1, 1}, {-1, 4}, {0, 9}, {3, 2}, {3, 7}};
    struct order order = {1, 0};
    sort_r(items, 0, sizeof *items, compare_items, &order);
    sort_r(items, 1, sizeof *items, compare_items, &order);
    CHECK(items[0].key == 3 && items[0].id == 7);
    sort_r(items, 5, sizeof *items, compare_items, &order);
    CHECK(order.calls > 0);
    for (size_t i = 0; i < 5; ++i)
        CHECK(items[i].key == ascending[i].key && items[i].id == ascending[i].id);
    order.direction = -1;
    order.calls = 0;
    sort_r(items, 5, sizeof *items, compare_items, &order);
    CHECK(order.calls > 0);
    for (size_t i = 0; i < 5; ++i)
        CHECK(items[i].key == ascending[4-i].key && items[i].id == ascending[4-i].id);
}

int main(void) {
    test_bitmap_zero_and_boundaries();
    test_bitmap_alloc_and_resize();
    test_sort();
    puts("bitmap and sort regression tests passed");
    return 0;
}
