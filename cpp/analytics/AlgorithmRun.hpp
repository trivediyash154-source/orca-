#pragma once

/// @file AlgorithmRun.hpp
/// A record of one completed algorithm run — Phase 11 brief §14-18's telemetry model.
///
/// ## Algorithms produce metrics; this file only aggregates them
///
/// `SearchMetrics` (Dijkstra/A*, Phase 4), `ParetoSearchMetrics` (Pareto, Phase 5), and
/// `AssignmentMetrics` (Hungarian/Greedy, Phase 6) already exist and are already populated
/// by their own algorithms, unmodified by this file. `AlgorithmRunRecord` is a plain,
/// dependency-free aggregator that a SIMULATION-layer caller (`EventDispatcher.cpp`) fills
/// in by copying an already-returned metrics struct plus minimal identifying context — the
/// algorithm modules themselves (`cpp/algorithms/`) do not know this type exists, and
/// nothing in `cpp/analytics/` depends on React, Node, or `SimulationState` (brief §17).

#include <optional>
#include <string>
#include <string_view>

#include "../algorithms/ParetoSearchMetrics.hpp"
#include "../algorithms/SearchMetrics.hpp"
#include "../core/Units.hpp"
#include "../optimization/AssignmentResult.hpp"

namespace orca {

enum class AlgorithmKind { Dijkstra, AStar, Pareto, Hungarian, Greedy };

[[nodiscard]] constexpr std::string_view toString(AlgorithmKind kind) noexcept {
    switch (kind) {
        case AlgorithmKind::Dijkstra: return "DIJKSTRA";
        case AlgorithmKind::AStar: return "ASTAR";
        case AlgorithmKind::Pareto: return "PARETO";
        case AlgorithmKind::Hungarian: return "HUNGARIAN";
        case AlgorithmKind::Greedy: return "GREEDY";
    }
    return "UNKNOWN";
}

/// One completed run. Exactly one of `search`/`pareto`/`assignment` is set, matching
/// `algorithm` — never fabricated for a kind that doesn't produce it (brief §15: "only
/// include metrics that actually exist for that algorithm").
struct AlgorithmRunRecord {
    AlgorithmKind algorithm = AlgorithmKind::Dijkstra;
    Seconds timestampSec = 0.0;

    /// Meaningless for assignment algorithms (Hungarian/Greedy solve a matrix, not a single
    /// source->target search) -- left at `true` for those, since "found" only means
    /// something for a path search.
    bool found = true;
    std::optional<Seconds> arrivalTimeSec;

    std::optional<SearchMetrics> search;          ///< Dijkstra, A*
    std::optional<ParetoSearchMetrics> pareto;     ///< Pareto
    std::optional<AssignmentMetrics> assignment;   ///< Hungarian, Greedy
};

}  // namespace orca
