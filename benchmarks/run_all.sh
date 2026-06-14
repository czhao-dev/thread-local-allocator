#!/usr/bin/env bash
# Runs each benchmark twice: once against the system allocator, and once
# with libmemalloc loaded in place of malloc/free (LD_PRELOAD on Linux,
# DYLD_INSERT_LIBRARIES on macOS). See README "Benchmarks".
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build}"
BIN_DIR="$BUILD_DIR/bin"
LIB_DIR="$BUILD_DIR/lib"

if [[ "$(uname)" == "Darwin" ]]; then
    LIB_PATH="$LIB_DIR/libmemalloc.dylib"
    PRELOAD_ENV=(env "DYLD_INSERT_LIBRARIES=$LIB_PATH" "DYLD_FORCE_FLAT_NAMESPACE=1")
else
    LIB_PATH="$LIB_DIR/libmemalloc.so"
    PRELOAD_ENV=(env "LD_PRELOAD=$LIB_PATH")
fi

for bench in fixed_size_bench mixed_size_bench latency_bench; do
    echo "############################################"
    echo "# $bench"
    echo "############################################"

    echo "--- system allocator ---"
    "$BIN_DIR/$bench"
    echo

    if [[ -f "$LIB_PATH" ]]; then
        echo "--- memalloc ---"
        "${PRELOAD_ENV[@]}" "$BIN_DIR/$bench"
        echo
    else
        echo "skipping memalloc run: $LIB_PATH not found (build first)"
        echo
    fi
done
