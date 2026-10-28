#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
cc=${CC:-cc}
"$cc" -std=c99 -Wall -Wextra -Werror -g -O1 -fno-omit-frame-pointer \
    -fsanitize=address,undefined tests/format-native/format.c \
    libonyxc/src/format/float.c -o "$build/test"
"$build/test"
