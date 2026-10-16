#pragma once

/// @file BenchmarkTypes.hpp
/// Shared data shapes for Phase 11's benchmark runner (brief §22-31). One flattened row
/// type, reused across path-search and assignment benchmarks, so `CsvWriter.cpp` has
/// exactly one shape to write regardless of which algorithm produced a row -- the same
/// "flattened projection, N/A fields genuinely absent" convention `cpp/io/`'s view types
/// already established for the exact same reason (a consumer should never have to guess
/// whether an empty field means zero or not-applicable).

#include <optional>
#include <string>

#include "../algorithms/ParetoSearchMetrics.hpp"
#include "../algorithms/SearchMetrics.hpp"
#include "../core/Types.hpp"
#include "../core/Units.hpp"
#include "../optimization/AssignmentResult.hpp"

namespace orca::analytics {

/// One (scenario, algorithm, seed, repetition) measurement. `repetition < 0` marks a
/// warm-up run (brief §24: "do not include warm-up timings in measured results") --
/// `computeStatistics()` (`Statistics.hpp`) excludes these automatically.
struct BenchmarkRow {
    std::string scenarioFamily;
    int objectCount = 0;
    Seed seed = 0;
    std::string algorithm;
    int repetition = 0;

    bool found = true;
    double executionTimeMs = 0.0;

    std::optional<SearchMetrics> search;
    std::optional<ParetoSearchMetrics> pareto;
    std::optional<AssignmentMetrics> assignment;

    /// Assignment benchmark only (brief §31): candidate-generation time is tracked
    /// SEPARATELY from solver time, because Phase 6 already found candidate generation
    /// dominates -- collapsing the two into one number would hide that result again.
    std::optional<double> candidateGenerationTimeMs;
    std::optional<double> solverTimeMs;
};

}  // namespace orca::analytics
