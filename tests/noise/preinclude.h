/* This translation unit only needs project scalar and math declarations;
   bypass util.h's unrelated graphics/vector headers. */
#define UTIL_H
#include <stdbool.h>
#include <math.h>
#include <string.h>
#include "src/util/fmath.h"
