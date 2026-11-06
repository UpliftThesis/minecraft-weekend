#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
export TMPDIR="$PWD/tests/noise"
test_binary=tests/noise/.noise-test-$$
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Ilib/noise tests/noise/test_noise.c lib/noise/noise1234.c -lm -o "$test_binary"
"$test_binary"
"${CC:-cc}" -std=gnu11 -Wall -Wextra -I. -Itests/math -Ilib/noise -include tests/noise/preinclude.h tests/noise/test_world_noise.c src/world/gen/noise.c lib/noise/noise1234.c -lm -o "$test_binary"
"$test_binary"
