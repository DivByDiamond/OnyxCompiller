#!/usr/bin/env bash
# cpp_runner.sh — compile & run the C++ fixtures in tests/cpp.
#
# Fixture contract:
#   // EXPECT: <text>        expected stdout (one line; \n is implied)
#   // EXPECT-COMPILE-FAIL   the compiler MUST reject this file
#
# Every other fixture is compiled with onyxcc (auto-linked libonyxc),
# executed with tools/onx-run, and its stdout compared byte-for-byte.
set -uo pipefail

ONYXCC="./onyxcc"
RUNNER="./tools/onx-run"
INC="-I libonyxc/include/core -I libonyxc/include/io -I libonyxc/include/ctype"
TMP="/tmp/onyxcc_cpp"
mkdir -p "$TMP"

if [ ! -x "$ONYXCC" ]; then
    echo "cpp_runner: $ONYXCC not built (run make first)" >&2
    exit 1
fi
if [ ! -x "$RUNNER" ]; then
    echo "cpp_runner: $RUNNER not built (run make tools/onx-run)" >&2
    exit 1
fi

total=0
pass=0
fail=0

shopt -s nullglob
for src in tests/cpp/*.cpp; do
    name=$(basename "$src" .cpp)
    out="$TMP/$name.onx"
    log="$TMP/$name.log"
    total=$((total + 1))

    if grep -q "EXPECT-COMPILE-FAIL" "$src"; then
        if $ONYXCC $INC -o "$out" "$src" >"$log" 2>&1; then
            echo "FAIL $name (expected the compiler to reject this file)"
            fail=$((fail + 1))
        else
            echo "PASS $name (rejected as expected)"
            pass=$((pass + 1))
        fi
        continue
    fi

    if ! $ONYXCC $INC -o "$out" "$src" >"$log" 2>&1; then
        echo "FAIL $name (compile)"
        sed -n '1,5p' "$log"
        fail=$((fail + 1))
        continue
    fi

    actual=$($RUNNER "$out" 2>&1)
    rc=$?
    expected=$(grep -o '// EXPECT: .*' "$src" | sed 's#// EXPECT: ##' | paste -sd '\n' -)
    expected_rc=0
    if grep -q 'EXPECT-EXIT:' "$src"; then
        expected_rc=$(grep -o 'EXPECT-EXIT: [0-9]*' "$src" | awk '{print $2}')
    fi
    if [ "$rc" -ne "$expected_rc" ]; then
        echo "FAIL $name (run exit $rc, expected $expected_rc)"
        echo "$actual" | sed -n '1,5p'
        fail=$((fail + 1))
    elif [ "$actual" != "$expected" ]; then
        echo "FAIL $name (stdout mismatch)"
        echo "  expected: $expected"
        echo "  actual:   $actual"
        fail=$((fail + 1))
    else
        echo "PASS $name"
        pass=$((pass + 1))
    fi
done
shopt -u nullglob

echo
echo "CPP RESULTS: $total total, $pass PASS, $fail FAIL"
[ "$fail" -eq 0 ]
