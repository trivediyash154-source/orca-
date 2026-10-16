#!/usr/bin/env bash
#
# Build the orca_engine binary without CMake.
#
# See scripts/run_tests.sh for why this exists. Flags mirror CMakeLists.txt.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${ROOT}/build-direct"
mkdir -p "${BUILD}"

CXX="${CXX:-c++}"

WARNINGS=(
  -Wall -Wextra -Wpedantic
  -Wshadow -Wconversion -Wsign-conversion
  -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
  -Wunused -Woverloaded-virtual -Wdouble-promotion
  -Werror
)

# Release settings, matching BENCHMARK_PLAN.md §2.4's requirement that every reported
# measurement comes from an -O2 -DNDEBUG build.
FLAGS=(-std=c++20 -O2 -DNDEBUG "${WARNINGS[@]}")

SOURCES=()
while IFS= read -r line; do SOURCES+=("${line}"); done < <(
  find "${ROOT}/cpp" -name '*.cpp' | sort)

echo "=== compiling ${#SOURCES[@]} sources ==="
"${CXX}" "${FLAGS[@]}" "${SOURCES[@]}" -o "${BUILD}/orca_engine"
echo "built: ${BUILD}/orca_engine"
