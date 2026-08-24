#!/usr/bin/env bash
# integration-runner.sh — compile AND execute tests through onx-run.
#
# Unlike scripts/test_runner.sh (compile-only), this runner:
#   1. Compiles each tests/run/*.c with onyxcc (auto-linked libonyxc)
#   2. Executes the .onx in the onx-run RV64 emulator
#   3. Compares stdout against the expected output embedded in the test
#
# Expected-output convention: each test's first line is
#     // EXPECT: <exact stdout>
# Multi-line expectations use multiple // EXPECT: lines.

set -uo pipefail

cd "$(dirname "$0")/.."

ONYXCC=./onyxcc
ONXRUN=./tools/onx-run
TEST_DIR=tests/run
TMPDIR_RUN=/tmp/onyx_run_tests
TIMEOUT=15

mkdir -p "$TMPDIR_RUN"

total=0
pass=0
fail=0
failures=()

for src in "$TEST_DIR"/*.c; do
    [ -e "$src" ] || continue
    name=$(basename "$src" .c)
    total=$((total + 1))

    # Extract expected output.
    expected=$(grep '^// EXPECT: ' "$src" | sed 's|^// EXPECT: ||')

    # Optional program arguments.
    args=$(grep -m1 '^// ARGS: ' "$src" | sed 's|^// ARGS: ||')

    out="$TMPDIR_RUN/$name.onx"

    # Compile.
    if ! timeout 30 "$ONYXCC" -o "$out" "$src" > "$TMPDIR_RUN/$name.cerr" 2>&1; then
        fail=$((fail + 1))
        failures+=("$name: COMPILE")
        continue
    fi

    # Execute.
    actual=$(timeout $TIMEOUT "$ONXRUN" "$out" $args 2>"$TMPDIR_RUN/$name.rerr")
    rc=$?
    if [ $rc -eq 124 ]; then
        fail=$((fail + 1))
        failures+=("$name: TIMEOUT")
        continue
    fi

    # Compare.
    if [ "$actual" == "$expected" ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        failures+=("$name: OUTPUT")
        echo "--- $name ---"
        echo "expected: $(echo "$expected" | head -3)"
        echo "actual:   $(echo "$actual" | head -3)"
    fi
done

echo ""
echo "======================================"
if [ ${#failures[@]} -gt 0 ]; then
    echo "FAILED:"
    for f in "${failures[@]}"; do echo "  FAIL $f"; done
fi
echo "RESULTS: $total total, $pass PASS, $fail FAIL"
[ $fail -eq 0 ]
