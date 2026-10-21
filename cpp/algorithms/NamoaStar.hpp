#pragma once

/// @file NamoaStar.hpp
/// NAMOA* (Mandow and Perez de la Cruz): multi-objective A* over ORCA's time-dependent
/// mission graph. Returns the same non-dominated set as `findParetoRoutes()` (Martins
/// label setting) on every input, but uses an admissible heuristic vector to discard
/// partial routes whose optimistic completion is already dominated by a found solution.
///
/// ## Heuristic vector
///
///     h(n) = ( h_time(n), 0, 0, |r(n) - r(goal)| )
///
/// * time: `astar_heuristic::estimateRemainingTimeSec`, the same admissible bound A* uses
///   (minimum of the direct Hohmann coast and the floor detour through Earth's radius).
/// * fuel, risk: 0. A multi-hop route can be cheaper in delta-v than the direct Hohmann
///   transfer (bi-elliptic effect), so no non-trivial delta-v bound is claimed.
/// * distance: every leg's path length is at least the change in radius it produces
///   (a pure plane change has length 0 and no radius change), so by the triangle
///   inequality on radius the sum over any route is at least |r(n) - r(goal)|.
///
/// Risk composes as 1 - (1 - a)(1 - b), which is monotone non-decreasing, so the risk of
/// any completion is at least the risk accumulated so far; with h_risk = 0 the f-vector
/// therefore never exceeds the cost of any completion on any component.
///
/// ## Soundness in the time-dependent graph
///
/// Edge delta-v, risk and distance do not depend on departure time, and arrival time is
/// non-decreasing in departure time (PhasingModel.hpp), provided the per-leg wait limit is
/// unbounded, which is how every planning call site in the engine configures it. Under
/// those conditions a label that dominates another at the same node can mirror every
/// continuation of the dominated label at no greater cost, so node-local pruning is safe.
///
/// ## Processing order
///
/// Open labels are expanded in lexicographic order of f = g + h (time first), which always
/// selects a label whose f-vector is non-dominated within OPEN.

#include "../graph/MissionGraph.hpp"
#include "../transfer/TransferModel.hpp"
#include "Dijkstra.hpp"
#include "ParetoSearch.hpp"

namespace orca {

struct NamoaMetrics {
    int labelsGenerated = 0;
    int labelsInserted = 0;
    int labelsDominatedAtNode = 0;
    int labelsFilteredBySolutions = 0;  ///< discarded because f is dominated by a found solution
    int labelsExpanded = 0;
    int heuristicEvaluations = 0;
    int dominanceComparisons = 0;
    int targetLabelCount = 0;
    bool truncated = false;
    double executionTimeMs = 0.0;
};

struct NamoaResult {
    std::vector<ParetoRoute> solutions;
    NamoaMetrics metrics;
    [[nodiscard]] bool isEmpty() const noexcept { return solutions.empty(); }
};

[[nodiscard]] NamoaResult findParetoRoutesNamoa(const MissionGraph& graph,
                                                const TransferModel& transferModel,
                                                NodeId source, NodeId target,
                                                Seconds startTimeSec,
                                                const SearchConstraints& constraints = {},
                                                int maxLabels = 100000);

}  // namespace orca
