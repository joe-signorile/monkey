#!/usr/bin/env bash
# Compiles Grid for the host and runs its checks. No Android toolchain needed.
set -euo pipefail
cd "$(dirname "$0")/.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
g++ -std=c++17 -Wall -Wextra -O1 \
    app/src/test/cpp/grid_test.cpp app/src/main/cpp/grid.cpp \
    -o "$out/grid_test"
"$out/grid_test"
