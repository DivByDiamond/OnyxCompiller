#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
cc=${CC:-cc}
"$cc" -std=c99 -Wall -Wextra -Werror -g -O1 -fno-omit-frame-pointer \
    -fsanitize=address,undefined -fno-pie -no-pie -Iinclude \
    tests/type-metadata-native/metadata.c src/core/types.c src/core/util.c \
    -o "$build/test"
"$build/test"
