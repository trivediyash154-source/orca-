#pragma once

/// @file AssignmentBenchmark.hpp
/// Greedy vs. Hungarian assignment comparison at controlled matrix sizes — Phase 11 brief
/// §31. Candidate-generation time and solver time are reported as SEPARATE fields on the
/// resulting `BenchmarkRow`s, not summed into one number: Phase 6 already found candidate
/// generation dominates total wall time, and collapsing the two again would hide that
/// result a second time.
///
/// ## Emergencies are synthetic, constructed directly for this benchmark
///
/// A standalone assignment benchmark needs `Emergency` objects, but `Scenario`/
/// `ScenarioGenerator` produce satellites, not emergencies (those are created at runtime by
/// `EventDispatcher.cpp` when a satellite actually fails, `SIMULATION_ENGINE.md` §10). This
/// file constructs one `Emergency` per generated satellite directly — a controlled,
/// deterministic benchmark fixture, not a claim that this is how emergencies arise during
/// an actual run.

#include <string>
#include <vector>

#include "../core/Types.hpp"
#include "../simulation/ScenarioGenerator.hpp"
#include "BenchmarkTypes.hpp"

namespace orca::analytics {

struct AssignmentBenchmarkConfig {
    std::string familyName;
    int matrixSize = 5;  ///< vehicleCount == satelliteCount == emergencyCount
    Seed seed = 0;
    int warmupRuns = 1;
    int repetitions = 5;
};

/// Generates a `matrixSize x matrixSize` scenario, builds one synthetic emergency per
/// satellite, runs candidate generation once (timed), then runs BOTH `solveAssignmentGreedy`
/// and `solveAssignmentHungarian` on the identical resulting matrix, each timed separately,
/// `warmupRuns + repetitions` times. Returns `2 * (warmupRuns + repetitions)` rows.
[[nodiscard]] std::vector<BenchmarkRow> runAssignmentBenchmark(const AssignmentBenchmarkConfig& config);

}  // namespace orca::analytics
