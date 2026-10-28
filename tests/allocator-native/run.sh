#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
cc=${CC:-cc}
flags=(-std=c99 -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined)
"$cc" "${flags[@]}" -fno-builtin -Dmalloc=onyx_malloc -Dfree=onyx_free \
    -Dcalloc=onyx_calloc -Drealloc=onyx_realloc \
    -Ilibonyxc/include/core -Ilibonyxc/include/io -Ilibonyxc/include/ctype \
    -c libonyxc/src/memory/alloc.c -o "$build/alloc.o"
"$cc" "${flags[@]}" tests/allocator-native/allocator.c "$build/alloc.o" -o "$build/test"
"$build/test"
