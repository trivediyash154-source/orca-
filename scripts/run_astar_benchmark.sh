#!/usr/bin/env bash
#
# Build and run the Phase 4 Dijkstra-vs-A* micro-benchmark.
#
# See scripts/run_tests.sh for why direct compilation exists (CMake is not installed --
# docs/REPOSITORY_AUDIT.md §3). This mirrors build_engine.sh's release settings, since
# BENCHMARK_PLAN.md §2.4 requires every reported measurement to come from an
# -O2 -DNDEBUG build, never the -O1 -g debug build the test suite uses.
#
# This is deliberately separate from build_engine.sh: it links benchmarks/
# dijkstra_vs_astar_bench.cpp's own main() against the engine sources MINUS
# cpp/app/main.cpp (which defines a competing main() for the real orca_engine binary).

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

FLAGS=(-std=c++20 -O2 -DNDEBUG "${WARNINGS[@]}")

ENGINE_SOURCES=()
while IFS= read -r line; do ENGINE_SOURCES+=("${line}"); done < <(
  find "${ROOT}/cpp" -name '*.cpp' -not -path "${ROOT}/cpp/app/main.cpp" | sort)

echo "=== compiling benchmark (${#ENGINE_SOURCES[@]} engine sources + bench main) ==="
"${CXX}" "${FLAGS[@]}" \
  "${ROOT}/benchmarks/dijkstra_vs_astar_bench.cpp" \
  "${ENGINE_SOURCES[@]}" \
  -o "${BUILD}/dijkstra_vs_astar_bench"

echo "=== running ==="
"${BUILD}/dijkstra_vs_astar_bench"
