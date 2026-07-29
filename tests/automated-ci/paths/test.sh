#!/bin/sh
set -e

RUNNER="${1:-../../../build/butterscotch}"
BASE="${2:-./path_test}"

GAME="${BASE}.ios"
RAW="${BASE}_raw_output.txt"
ACTUAL="${BASE}_actual_output.txt"
EXPECTED="${BASE}_expected_output.txt"

stdbuf -oL -eL "$RUNNER" "$GAME" --headless > "$RAW" 2>&1

grep '^Game: ' "$RAW" | sed 's/^Game: //' > "$ACTUAL"

diff -u "$EXPECTED" "$ACTUAL"