/* The checkout lacks cglm; substitute only ECS's unused application types. */
#ifndef TEST_ECS_SHIM_H
#define TEST_ECS_SHIM_H
#define UTIL_H
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../../src/util/types.h"
#include "../../src/util/bitmap.h"
/* Real component registration depends on graphics and input subsystems.
   These tests register small observable components using the same ECS API. */
#define ECSCOMPONENTS_H
enum ECSComponent { TEST_VALUE, TEST_OTHER, ECSCOMPONENT_LAST = TEST_OTHER };
struct ECS;
void _ecs_init_internal(struct ECS *ecs);
#endif
