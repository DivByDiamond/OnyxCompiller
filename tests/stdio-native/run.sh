#!/usr/bin/env bash
# Keep host libc/sanitizer stdio intact by renaming all defined library symbols.
set -euo pipefail
cd "$(dirname "$0")/../.."
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
cc=${CC:-cc}
flags=(-std=c99 -g -O1 -fno-builtin -fno-omit-frame-pointer -fsanitize=address,undefined)
includes=(-Ilibonyxc/include/core -Ilibonyxc/include/io -Ilibonyxc/include/ctype)
"$cc" "${flags[@]}" "${includes[@]}" -ffunction-sections -fdata-sections \
    -c libonyxc/src/io/stdio.c -o "$build/stdio-base.o"
nm -g --defined-only "$build/stdio-base.o" |
    awk '$2 ~ /^[TBD]$/ { print $3 " onyx_" $3 }' > "$build/symbols"
objcopy --redefine-syms="$build/symbols" "$build/stdio-base.o" "$build/stdio.o"
"$cc" "${flags[@]}" "${includes[@]}" -Wl,--gc-sections \
    tests/stdio-native/stdio.c libonyxc/src/format/float.c "$build/stdio.o" -o "$build/test"
"$build/test"
echo 'native stdio: PASS'
