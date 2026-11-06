#!/bin/sh
# Runs both regression suites: the original headless checks (tests/run.sh) and
# the extended suite (tests/extended/run.sh). Exits non-zero if either fails.
set -u
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
status=0
printf '######## original suite (tests/run.sh)\n'
sh "$here/run.sh" || status=1
printf '\n######## extended suite (tests/extended/run.sh)\n'
sh "$here/extended/run.sh" "$@" || status=1
[ "$status" = 0 ] && printf '\nALL SUITES PASSED\n' || printf '\nSOME SUITE FAILED\n'
exit $status
