#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
cc=${CC:-cc}
TMPDIR=$(pwd)/tests/math
export TMPDIR
binary=$(mktemp tests/math/.math-test.XXXXXX)
trap 'rm -f "$binary"' EXIT HUP INT TERM
"$cc" -std=gnu11 -Wall -Wextra -Itests/math -include tests/math/preinclude.h tests/math/test_math.c src/util/fmath.c src/util/direction.c -lm -o "$binary"
"$binary"

"$cc" -std=gnu11 -Wall -Wextra -Itests/math tests/math/test_world.c -lm -o "$binary"
"$binary"
