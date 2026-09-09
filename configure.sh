#!/usr/bin/env sh
# ============================================================================
#  configure.sh - Generate the CMake build tree.
#
#  Usage:
#    ./configure.sh                        Generates into ./build (Release)
#    ./configure.sh -DCMAKE_BUILD_TYPE=Debug
#    ./configure.sh ...                    any extra CMake options
# ============================================================================
set -e

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
BUILD_DIR="$ROOT/build"

found_build_type=0
for arg in "$@"; do
    case "$arg" in
        *CMAKE_BUILD_TYPE*) found_build_type=1 ;;
    esac
done

if [ "$found_build_type" -eq 0 ]; then
    cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release "$@"
else
    cmake -S "$ROOT" -B "$BUILD_DIR" "$@"
fi
