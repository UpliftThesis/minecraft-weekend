#!/bin/sh
set -eu
cd "$(dirname "$0")"
# Keep generated files within this new test directory.
TMPDIR="$PWD"
export TMPDIR
binary="./bitmap_sort_test.$$"
trap 'rm -f "$binary"' EXIT HUP INT TERM
"${CC:-gcc}" -std=c11 -Wall -Wextra -Wpedantic test_bitmap_sort.c -o "$binary"
"$binary"
