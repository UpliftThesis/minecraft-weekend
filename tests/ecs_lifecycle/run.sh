#!/bin/sh
set -eu
cd "$(dirname "$0")"
TMPDIR="$PWD"
export TMPDIR
binary="./ecs_test.$$"
trap 'rm -f "$binary"' EXIT HUP INT TERM
"${CC:-gcc}" -std=gnu11 -Wall -Wextra -include ./shim.h test_ecs.c ../../src/entity/ecs.c -o "$binary"
"$binary"
