#!/usr/bin/env bash
#
# Build and run the ORCA test suite without CMake.
#
# CMake is not installed on the development machine (docs/REPOSITORY_AUDIT.md §3), so this
# script compiles the engine and its tests directly with the system compiler. It exists so
# Phase 1 is *verifiable now* rather than verifiable once a dependency is installed.
#
# When CMake becomes available, `cmake -B build && cmake --build build && ctest` is the
# supported path and this script becomes a convenience. The compiler flags below are kept
# identical to those in CMakeLists.txt so that neither path is more permissive.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${ROOT}/build-direct"
mkdir -p "${BUILD}"

CXX="${CXX:-c++}"

# Mirrors the warning set in CMakeLists.txt. -Werror is deliberate: a warning that is not
# an error is a warning nobody fixes.
WARNINGS=(
  -Wall -Wextra -Wpedantic
  -Wshadow -Wconversion -Wsign-conversion
  -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
  -Wunused -Woverloaded-virtual -Wdouble-promotion
  -Werror
)

# Debug build with assertions enabled: the domain uses assert() for internal invariants,
# and a test run that compiled them out would not be exercising them.
FLAGS=(-std=c++20 -O1 -g "${WARNINGS[@]}")

# Read file lists without `mapfile`: macOS ships bash 3.2, which does not have it.
ENGINE_SOURCES=()
while IFS= read -r line; do ENGINE_SOURCES+=("${line}"); done < <(
  find "${ROOT}/cpp/core" "${ROOT}/cpp/domain" "${ROOT}/cpp/transfer" "${ROOT}/cpp/graph" \
       "${ROOT}/cpp/algorithms" "${ROOT}/cpp/optimization" "${ROOT}/cpp/planning" \
       "${ROOT}/cpp/simulation" "${ROOT}/cpp/io" "${ROOT}/cpp/analytics" \
       "${ROOT}/cpp/orbital" -name '*.cpp' | sort)

TEST_SOURCES=()
while IFS= read -r line; do TEST_SOURCES+=("${line}"); done < <(
  find "${ROOT}/tests" -name '*.cpp' | sort)

echo "=== compiling ${#ENGINE_SOURCES[@]} engine + ${#TEST_SOURCES[@]} test sources ==="

OBJECTS=()
for src in "${ENGINE_SOURCES[@]}" "${TEST_SOURCES[@]}"; do
  obj="${BUILD}/$(echo "${src#"${ROOT}/"}" | tr '/' '_').o"
  "${CXX}" "${FLAGS[@]}" -c "${src}" -o "${obj}"
  OBJECTS+=("${obj}")
done

"${CXX}" "${FLAGS[@]}" "${OBJECTS[@]}" -o "${BUILD}/orca_tests"

echo "=== running ==="
"${BUILD}/orca_tests"
