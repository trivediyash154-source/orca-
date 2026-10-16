#pragma once

/// @file SearchMetrics.hpp
/// Diagnostic counters for a single search run — shared verbatim between Dijkstra and A*
/// (Phase 4), since both report exactly this set of counters and a second, parallel
/// struct would only invite the two to drift apart. `heuristicEvaluations` is always 0
/// for Dijkstra (it calls no heuristic) and is what makes the two algorithms' counters
/// directly comparable side by side.
///
/// These are algorithm counters, not a benchmark. `executionTimeMs` is measured once,
/// centrally, around the whole search loop (Dijkstra.cpp, AStar.cpp) — never scattered as
/// per-iteration timing calls, which would both distort what is being measured and make
/// the code harder to read. A single run's timing on a development machine is a diagnostic
/// number, not a benchmark result; treating it as the latter is exactly what
/// BENCHMARK_PLAN.md's protocol (warm-up, repetitions, median-of-many) exists to avoid.
///
/// The counters *are* reproducible and portable in the way BENCHMARK_PLAN.md §3.2
/// discusses for algorithm counters generally: `nodesExpanded` and `edgesEvaluated` for a
/// given graph, source, sink and constraints are identical on any machine, any run.
///
/// `goalChecks` was considered and deliberately omitted: both searches check for the goal
/// exactly once per finalised node (immediately after popping it), so the count would
/// always equal `nodesExpanded` and add a field with no information of its own —
/// exactly the kind of metric the Phase 4 brief's §23 warns against keeping "if useful"
/// turns out to mean "if actually distinct".

namespace orca {

struct SearchMetrics {
    int nodesExpanded = 0;       ///< labels popped and finalised (not stale)
    int edgesEvaluated = 0;      ///< TransferModel::evaluate() calls made
    int relaxationsSucceeded = 0;  ///< edges that improved a node's best arrival
    int relaxationsRejected = 0;   ///< edges evaluated but infeasible or not an improvement
    int priorityQueuePushes = 0;
    int priorityQueuePops = 0;
    int staleEntriesDiscarded = 0;  ///< lazy-deletion hits (ALGORITHMS.md §2.4)
    int heuristicEvaluations = 0;   ///< A* only; always 0 for Dijkstra
    double executionTimeMs = 0.0;
};

}  // namespace orca
