#!/usr/bin/env bash
#
# Build and run the Phase 5 Pareto search micro-benchmark.
#
# See scripts/run_astar_benchmark.sh for why this pattern exists (CMake not installed --
# docs/REPOSITORY_AUDIT.md §3; release flags per BENCHMARK_PLAN.md §2.4).

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
  "${ROOT}/benchmarks/pareto_search_bench.cpp" \
  "${ENGINE_SOURCES[@]}" \
  -o "${BUILD}/pareto_search_bench"

echo "=== running ==="
"${BUILD}/pareto_search_bench"
