#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
cc=${CC:-cc}
flags=(-std=c99 -Wall -Wextra -Werror -g -O1 -fno-builtin -fno-omit-frame-pointer -fsanitize=address,undefined)
"$cc" "${flags[@]}" -Dstrtod=onyx_strtod \
    -Ilibonyxc/include/core -Ilibonyxc/include/io -Ilibonyxc/include/ctype \
    -c libonyxc/src/convert/strtod.c -o "$build/strtod.o"
"$cc" "${flags[@]}" tests/strtod-native/strtod.c "$build/strtod.o" -o "$build/test"
"$build/test"
