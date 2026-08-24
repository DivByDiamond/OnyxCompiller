#!/usr/bin/env bash
set -euo pipefail

TIMEOUT_SEC=10

# OnyxCC test runner - compiles all tests/*.c and reports PASS/FAIL.

ONYXCC="./onyxcc"
TEST_DIR="tests"
TMPDIR="/tmp/onyxcc_test"
INCLUDES="-N -I libonyxc/include/core -I libonyxc/include/io -I libonyxc/include/ctype -I include -I include/core -I include/front -I include/back -I include/arch -I include/sys"
VERBOSE=false

while getopts "v" opt; do
    case $opt in
        v) VERBOSE=true ;;
        *) echo "Usage: $0 [-v]"; exit 1 ;;
    esac
done

if [ ! -x "$ONYXCC" ]; then
    echo "ERROR: $ONYXCC not found or not executable. Build it first with 'make'."
    exit 1
fi

mkdir -p "$TMPDIR"

total=0
pass=0
fail=0
skip=0
failures=""

shopt -s nullglob
test_files=("$TEST_DIR"/*.c)
shopt -u nullglob

for src in "${test_files[@]}"; do
    name=$(basename "$src" .c)
    total=$((total + 1))

    out="$TMPDIR/$name.onx"

    set +e
    stderr=$(timeout "$TIMEOUT_SEC" "$ONYXCC" $INCLUDES -o "$out" "$src" 2>&1)
    rc=$?
    set -e

    if [ "$rc" -eq 124 ]; then
        fail=$((fail + 1))
        failures="${failures}  FAIL  $name - timed out (>${TIMEOUT_SEC}s)\n"
        if [ "$VERBOSE" = true ]; then
            echo "  FAIL  $name - timed out (>${TIMEOUT_SEC}s)"
        fi
        continue
    fi

    if [ "$rc" -eq 0 ]; then
        if [ -f "$out" ]; then
            pass=$((pass + 1))
            if [ "$VERBOSE" = true ]; then
                echo "  PASS  $name"
            fi
        else
            fail=$((fail + 1))
            msg="no output file produced"
            failures="${failures}  FAIL  $name - $msg\n"
            if [ "$VERBOSE" = true ]; then
                echo "  FAIL  $name - $msg"
            fi
        fi
    else
        fail=$((fail + 1))
        first_line=$(echo "$stderr" | head -1)
        failures="${failures}  FAIL  $name - $first_line\n"
        if [ "$VERBOSE" = true ]; then
            echo "  FAIL  $name - $first_line"
        fi
    fi
done

echo ""
echo "========================================"
echo "RESULTS: $total total, $pass PASS, $fail FAIL, $skip SKIP"
echo "========================================"

if [ -n "$failures" ]; then
    echo ""
    echo "FAILED TESTS:"
    printf "%b" "$failures"
fi

rm -rf "$TMPDIR"

if [ "$fail" -gt 0 ]; then
    exit 1
fi
exit 0
