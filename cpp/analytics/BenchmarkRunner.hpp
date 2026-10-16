#pragma once

/// @file BenchmarkRunner.hpp
/// Reproducible Dijkstra / A* / Pareto search comparison over a generated scenario —
/// Phase 11 brief §22-26. One (vehicle, satellite) pair per scenario -- the first vehicle
/// to the first satellite, a fixed, documented choice so the comparison is apples-to-apples
/// across algorithms and repetitions.
///
/// ## Warm-up, repetitions, and what varies
///
/// The search RESULT (route, cost, every counter in `SearchMetrics`/`ParetoSearchMetrics`)
/// is deterministic and identical across repetitions — only `executionTimeMs` varies, due
/// to ordinary system noise (`SearchMetrics.hpp`'s own file comment already states this).
/// `warmupRuns` executions run first and are discarded (never included in a `BenchmarkRow`
/// with `repetition >= 0`); `repetitions` further executions are then recorded.

#include <string>
#include <vector>

#include "../core/Types.hpp"
#include "../simulation/ScenarioGenerator.hpp"
#include "BenchmarkTypes.hpp"

namespace orca::analytics {

struct SearchBenchmarkConfig {
    std::string familyName;
    ScenarioGeneratorConfig scenarioConfig;
    Seed seed = 0;
    int warmupRuns = 1;
    int repetitions = 5;
};

/// Generates the scenario, builds its graph once, then runs Dijkstra, A*, and Pareto
/// search from the first vehicle to the first satellite `warmupRuns + repetitions` times
/// each. Returns one `BenchmarkRow` per (algorithm, run) -- `3 * (warmupRuns + repetitions)`
/// rows total, warm-up rows carrying `repetition < 0`.
///
/// @throws std::invalid_argument if the generated scenario has no vehicles or satellites
///         (should not happen for any of the eight documented families, but is checked
///         explicitly rather than assumed).
[[nodiscard]] std::vector<BenchmarkRow> runSearchComparisonBenchmark(const SearchBenchmarkConfig& config);

}  // namespace orca::analytics
