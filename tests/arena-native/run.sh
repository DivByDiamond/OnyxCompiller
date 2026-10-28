#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
cc=${CC:-cc}
flags=(-std=c99 -Wall -Wextra -Werror -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined)
"$cc" "${flags[@]}" -Iinclude -Dmalloc=arena_test_malloc -Dfree=arena_test_free \
    -c src/core/util.c -o "$build/util.o"
"$cc" "${flags[@]}" -Iinclude tests/arena-native/arena.c "$build/util.o" -o "$build/test"
"$build/test"
