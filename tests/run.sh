#!/bin/sh
# Run from any working directory; all build artifacts stay under tests/.
set -eu
suite_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
for suite in bitmap_sort noise math ecs_lifecycle; do
    printf '\n== %s ==\n' "$suite"
    sh "$suite_dir/$suite/run.sh"
done
printf '\nAll regression checks passed.\n'
