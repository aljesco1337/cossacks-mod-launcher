#!/usr/bin/env sh
# ============================================================================
#  build-release.sh - Build Release binaries (configures first if needed).
#
#  Usage:
#    ./build-release.sh             Builds the Release configuration
#    ./build-release.sh -j4         ...or pass any extra CMake build options
# ============================================================================
set -e

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
BUILD_DIR="$ROOT/build"

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "Configuring CMake build tree..."
    "$ROOT/configure.sh"
fi

cmake --build "$BUILD_DIR" --config Release "$@"
