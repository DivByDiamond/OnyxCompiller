#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
cc=${CC:-cc}
flags=(-std=c99 -Wall -Wextra -Werror -g -O1 -fno-builtin -fno-omit-frame-pointer -fsanitize=address,undefined)
"$cc" "${flags[@]}" -Dsystem=onyx_system -Dsignal=onyx_signal \
    -Ilibonyxc/include/core -Ilibonyxc/include/io -Ilibonyxc/include/ctype \
    libonyxc/src/process/runtime.c tests/process-native/process.c -o "$build/test"
"$build/test"
echo 'native process: PASS'
