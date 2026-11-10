#!/usr/bin/env bash
# sepcomp-runner.sh — separate-compilation pipeline check.
#
# Pipeline under test:
#   onyxcc -N -c <src> -o <obj>          one ONYO object per translation unit
#   onyx-ld -o prog.onx -e _start <objs> link all objects
#   tools/onx-run prog.onx               execute, compare stdout byte-for-byte
#
# Fixture contract:
#   // EXPECT: <text>   expected stdout line (one per line, in order)
#
# Positive cases:
#   split-compile-link-run  C fixtures + all libonyxc objects
#   cpp-objects-link-run    C++ `-c` object: non-polymorphic class (its
#                           reserved vtable must NOT leak as UNDEF) and a
#                           polymorphic pair (vtable lives in .data)
# Negative cases (onyx-ld must fail):
#   dup_a.c + dup_b.c   duplicate global definition
#   undef_main.c        undefined symbol
set -u

cd "$(dirname "$0")/.."

ONYXCC="./onyxcc"
LD="./onyx-ld"
RUNNER="./tools/onx-run"
INC="-I libonyxc/include/core -I libonyxc/include/io -I libonyxc/include/ctype"
TMP="/tmp/onyxcc_sep"
FIX="tests/separate"

for f in "$ONYXCC" "$LD" "$RUNNER"; do
    if [ ! -x "$f" ]; then
        echo "sepcomp: $f not built (run make first)" >&2
        exit 1
    fi
done

rm -rf "$TMP"
mkdir -p "$TMP/lib" "$TMP/c" "$TMP/cpp" "$TMP/neg"

# Capability probe for `onyxcc -c`. Report a missing -c once instead of
# dumping the usage text for every translation unit below.
if ! $ONYXCC -N -c -o "$TMP/probe.o" "$FIX/lib_math.c" >"$TMP/probe.log" 2>&1 &&
   grep -q "invalid option" "$TMP/probe.log"; then
    echo "sepcomp: onyxcc does not implement -c yet (separate compilation not landed)" >&2
    exit 2
fi
rm -f "$TMP/probe.o"

# libonyxc sources linked into every executable. Keep in sync with
# libsrcs[] in src/c/driver/libpath.c (17 entries).
LIB_SRCS="
libonyxc/src/core/start_cc.c
libonyxc/src/core/syscalls.c
libonyxc/src/io/stdio.c
libonyxc/src/io/stdlib.c
libonyxc/src/memory/alloc.c
libonyxc/src/io/string.c
libonyxc/src/io/strerror.c
libonyxc/src/ctype/ctype.c
libonyxc/src/io/time.c
libonyxc/src/io/termios.c
libonyxc/src/io/math.c
libonyxc/src/math/ieee.c
libonyxc/src/locale/locale.c
libonyxc/src/process/runtime.c
libonyxc/src/format/float.c
libonyxc/src/convert/strtod.c
libonyxc/src/eh/eh.c
"

total=0
pass=0
fail=0

pass_case() { total=$((total + 1)); pass=$((pass + 1)); echo "PASS $1"; }
fail_case() { total=$((total + 1)); fail=$((fail + 1)); echo "FAIL $1${2:+ ($2)}"; }

# compile_obj <src> <obj> — one translation unit to one ONYO object.
compile_obj() {
    if ! $ONYXCC -N -c $INC -o "$2" "$1" >"$TMP/cc.log" 2>&1; then
        echo "  compile failed: $1"
        sed -n '1,5p' "$TMP/cc.log"
        return 1
    fi
    return 0
}

# run_program <name> <expect-src> <out.onx> <objects...>
run_program() {
    local name="$1" expect_src="$2" out="$3"
    shift 3
    if ! $LD -o "$out" -e _start "$@" >"$TMP/$name.ld.log" 2>&1; then
        fail_case "$name" "link"
        sed -n '1,5p' "$TMP/$name.ld.log"
        return
    fi
    local actual rc expected
    actual=$($RUNNER "$out" 2>&1)
    rc=$?
    expected=$(grep -o '// EXPECT: .*' "$expect_src" | sed 's#^// EXPECT: ##')
    if [ "$rc" -ne 0 ]; then
        fail_case "$name" "run exit $rc"
        echo "$actual" | sed -n '1,5p'
    elif [ "$actual" != "$expected" ]; then
        fail_case "$name" "stdout mismatch"
        echo "  expected: $expected"
        echo "  actual:   $actual"
    else
        pass_case "$name"
    fi
}

# ── Positive cases ───────────────────────────────────────────────────────

bad=0
n=0
for src in $LIB_SRCS; do
    n=$((n + 1))
    compile_obj "$src" "$TMP/lib/lib_$n.o" || bad=1
done
compile_obj "$FIX/lib_math.c" "$TMP/c/lib_math.o" || bad=1
compile_obj "$FIX/main.c" "$TMP/c/main.o" || bad=1
compile_obj "$FIX/cpp_objects.cpp" "$TMP/cpp/cpp_objects.o" || bad=1

lib_objs=()
for o in "$TMP"/lib/*.o; do
    [ -e "$o" ] && lib_objs+=("$o")
done

if [ "$bad" -ne 0 ]; then
    fail_case "split-compile-link-run" "object compilation"
    fail_case "cpp-objects-link-run" "object compilation"
elif [ "${#lib_objs[@]}" -eq 0 ]; then
    fail_case "split-compile-link-run" "no library objects"
    fail_case "cpp-objects-link-run" "no library objects"
else
    run_program "split-compile-link-run" "$FIX/main.c" "$TMP/prog_c.onx" \
        "${lib_objs[@]}" "$TMP/c/main.o" "$TMP/c/lib_math.o"
    run_program "cpp-objects-link-run" "$FIX/cpp_objects.cpp" "$TMP/prog_cpp.onx" \
        "${lib_objs[@]}" "$TMP/cpp/cpp_objects.o"
fi

# ── Negative case: duplicate global definition must be rejected ─────────

bad=0
compile_obj "$FIX/dup_a.c" "$TMP/neg/dup_a.o" || bad=1
compile_obj "$FIX/dup_b.c" "$TMP/neg/dup_b.o" || bad=1

if [ "$bad" -ne 0 ]; then
    fail_case "duplicate-global-rejected" "object compilation"
elif $LD -o "$TMP/neg/dup.onx" -e _start \
        "$TMP/neg/dup_a.o" "$TMP/neg/dup_b.o" >"$TMP/neg/dup.log" 2>&1; then
    fail_case "duplicate-global-rejected" "link unexpectedly succeeded"
else
    pass_case "duplicate-global-rejected"
fi

# ── Negative case: undefined symbol must be rejected ────────────────────

if ! compile_obj "$FIX/undef_main.c" "$TMP/neg/undef_main.o"; then
    fail_case "undefined-symbol-rejected" "object compilation"
elif $LD -o "$TMP/neg/undef.onx" -e _start \
        "$TMP/neg/undef_main.o" >"$TMP/neg/undef.log" 2>&1; then
    fail_case "undefined-symbol-rejected" "link unexpectedly succeeded"
else
    pass_case "undefined-symbol-rejected"
fi

echo
echo "SEPCOMP RESULTS: $total total, $pass PASS, $fail FAIL"
[ "$fail" -eq 0 ]
