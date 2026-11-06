#!/bin/sh
# Line coverage of src/ achieved by the extended suite (gcc + gcov).
#   tests/extended/coverage.sh
# Builds into tests/extended/build/coverage and prints a per-file table.
set -u
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../.." && pwd)
build="$here/build/coverage"
rm -rf "$build"
CC=${CC:-gcc} EXTRA_CFLAGS="--coverage -O0" BUILD_DIR="$build" sh "$here/run.sh" >"$build.log" 2>&1
echo "suite exit status: $? (log: $build.log)"
cd "$build/obj" || exit 1
printf '%-34s %8s %8s %7s\n' file lines covered percent
total=0; hit=0
for gcno in src_*.gcno; do
    [ -f "$gcno" ] || continue
    out=$(gcov -n "$gcno" 2>/dev/null | awk '/^File .*src\//{f=$2} /^Lines executed/{ if (f!="") {print f, $0}; f="" }' | head -n 1)
    [ -n "$out" ] || continue
    file=$(printf '%s' "$out" | sed "s|^'[^']*/src/|src/|; s|' .*||")
    pct=$(printf '%s' "$out" | sed 's/.*executed:\([0-9.]*\)% of \([0-9]*\).*/\1/')
    n=$(printf '%s' "$out" | sed 's/.*executed:\([0-9.]*\)% of \([0-9]*\).*/\2/')
    c=$(awk -v p="$pct" -v n="$n" 'BEGIN{printf "%d", p*n/100 + 0.5}')
    total=$((total + n)); hit=$((hit + c))
    printf '%-34s %8s %8s %6s%%\n' "$file" "$n" "$c" "$pct"
done
awk -v t="$total" -v h="$hit" 'BEGIN{printf "%-34s %8d %8d %6.1f%%\n", "TOTAL (src/, excluding main.c)", t, h, (t ? 100*h/t : 0)}'
