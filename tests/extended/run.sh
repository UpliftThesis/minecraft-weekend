#!/bin/sh
# Extended headless regression suite.
#
#   tests/extended/run.sh [test-name-substring]
#
# Environment:
#   CC          C compiler (default: cc). Needs GNU C extensions (gcc or clang).
#   SANITIZE=1  build everything with AddressSanitizer + UBSan.
#   EXTRA_CFLAGS  appended to every compile.
#   JOBS        parallel compile jobs (default: 8).
#   TESTS       space-separated test file names to run (e.g. "test_light test_sky");
#               default: every test_*.c in this directory.
#   BUILD_DIR   build directory (default: tests/extended/build/<config>).
#   SRC_ROOT    directory containing the src/ tree to test (default: the repository
#               root). Used to run the suite against a modified copy, e.g. for
#               mutation testing or to check that a fix turns an XFAIL into XPASS.
#
# Everything is built under tests/extended/build/; no repository file outside
# tests/ is written. Production sources from src/ are compiled unmodified
# (except src/main.c, which owns the game loop) together with:
#   third_party/cglm   the cglm headers at the commit pinned by the lib/cglm submodule
#   lib/glad, lib/noise, lib/stb  (vendored in the repository)
#   support/           fake GLFW/OpenGL, world-building harness, test framework
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../.." && pwd)
src_root=$(CDPATH= cd -- "${SRC_ROOT:-$root}" && pwd)
cc=${CC:-cc}
jobs=${JOBS:-8}
filter=${1:-}

cflags="-std=gnu11 -g -O1 -fno-strict-aliasing"
# glibc needs _GNU_SOURCE to declare qsort_r. Define it only on Linux: on macOS it
# would make src/util/sort.h pick the glibc qsort_r argument order, which crashes.
if [ "$(uname -s)" = Linux ]; then
    cflags="$cflags -D_GNU_SOURCE"
fi
config=default
if [ "${SANITIZE:-0}" = 1 ]; then
    cflags="$cflags -fsanitize=address,undefined -fno-omit-frame-pointer"
    config=sanitize
    # The game intentionally leaks some allocations at shutdown; leaks are not under test.
    ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0}
    export ASAN_OPTIONS
fi
cflags="$cflags ${EXTRA_CFLAGS:-}"
incs="-I$src_root/src -I$here/third_party/cglm/include -I$root/lib/glad/include -I$here/support -I$root/lib/stb -I$root/lib/noise"

build="${BUILD_DIR:-$here/build/$config}"
mkdir -p "$build/obj" "$build/bin"
TMPDIR="$build"; export TMPDIR

# Objects are reused only if they were compiled with the same compiler and flags;
# otherwise drop them all so a flag change (e.g. after a git pull) takes effect.
flags_key="$cc $cflags $incs"
if [ ! -f "$build/flags.txt" ] || [ "$(cat "$build/flags.txt")" != "$flags_key" ]; then
    rm -f "$build"/obj/*.o
    printf '%s\n' "$flags_key" > "$build/flags.txt"
fi

# ---- 1. headless game library (all of src/ except main.c) ----
printf '== building headless game library (%s)\n' "$config"
srcs=$(cd "$src_root" && find src -name '*.c' ! -path src/main.c | sort)
printf '%s\n' $srcs lib/glad/src/glad.c lib/noise/noise1234.c support/fake_platform.c support/harness.c > "$build/sources.txt"

compile_one() {
    # $1 = path relative to repo root, or support/... relative to tests/extended
    case "$1" in
        support/*) src="$here/$1"; warn="-Wall -Wextra -Wno-unused-parameter -Wno-missing-braces -Wno-missing-field-initializers -Wno-sign-compare -Wno-unused-function" ;;
        src/*)     src="$src_root/$1"; warn="-w" ;;
        *)         src="$root/$1"; warn="-w" ;;
    esac
    obj="$build/obj/$(printf '%s' "$1" | tr '/' '_').o"
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ] || [ -n "$(find "$src_root/src" "$here/support" -name '*.h' -newer "$obj" 2>/dev/null | head -n 1)" ]; then
        # shellcheck disable=SC2086
        $cc $cflags $warn $incs -c "$src" -o "$obj"
    fi
}

failed_build=0
pids=""
n=0
for s in $(cat "$build/sources.txt"); do
    compile_one "$s" &
    pids="$pids $!"
    n=$((n + 1))
    if [ "$n" -ge "$jobs" ]; then
        for p in $pids; do wait "$p" || failed_build=1; done
        pids=""; n=0
    fi
done
for p in $pids; do wait "$p" || failed_build=1; done
[ "$failed_build" = 0 ] || { echo "build of game library failed"; exit 1; }

lib="$build/libgame_headless.a"
rm -f "$lib"
ar rcs "$lib" "$build"/obj/*.o

# ---- 2. tests ----
status=0
summary=""
if [ -n "${TESTS:-}" ]; then
    files=""; for n in $TESTS; do files="$files $here/${n%.c}.c"; done
else
    files=$(ls "$here"/test_*.c)
fi
for t in $files; do
    name=$(basename "$t" .c)
    printf '\n== %s\n' "$name"
    bin="$build/bin/$name"
    # shellcheck disable=SC2086
    if ! $cc $cflags -Wall -Wextra -Wno-unused-parameter -Wno-missing-braces -Wno-missing-field-initializers -Wno-sign-compare -Wno-unused-function $incs "$t" "$lib" -lm -o "$bin"; then
        echo "  BUILD FAILED"
        status=1; summary="$summary\n  BUILD-FAIL $name"; continue
    fi
    if (cd "$root" && "$bin" $filter); then
        summary="$summary\n  ok         $name"
    else
        status=1; summary="$summary\n  FAILED     $name"
    fi
done

printf '\n== extended suite summary'
printf "$summary\n"
if [ "$status" = 0 ]; then echo "Extended suite passed (known defects reported as XFAIL)."; else echo "Extended suite FAILED."; fi
exit $status
