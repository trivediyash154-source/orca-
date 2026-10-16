#pragma once

/// @file ParetoSearchMetrics.hpp
/// Diagnostic counters for one `findParetoRoutes()` run. Deliberately a separate struct
/// from `SearchMetrics.hpp` (Dijkstra/A*'s), not an extension of it: label-setting counts
/// fundamentally different things (labels, not a single best-value-per-node), and folding
/// both into one struct would leave single-objective-only or multi-objective-only fields
/// meaningless depending on which search populated them. `algorithm` name strings are how
/// `AlgorithmMetrics` (`DATA_MODEL.md` §7) tells the two apart in exported data.
///
/// Every counter here is measured by `ParetoSearch.cpp` as it runs — none are estimated,
/// and none are added unless `ParetoSearch.cpp` can actually populate them accurately
/// (Phase 5 brief §37).

namespace orca {

struct ParetoSearchMetrics {
    int labelsGenerated = 0;    ///< candidate labels produced by relaxing an edge
    int labelsInserted = 0;     ///< candidates that survived into a node's non-dominated set
    int labelsDominated = 0;    ///< candidates discarded because an existing label beat them
    int labelsDuplicate = 0;    ///< candidates discarded as equivalent to an existing label
    int labelsPruned = 0;       ///< candidates discarded by optional epsilon-dominance
    int labelsSuperseded = 0;   ///< previously-inserted labels removed because a new one
                                 ///< dominates them (ALGORITHMS.md §5.4's "remove M")
    int labelsExpanded = 0;     ///< labels popped from the queue and not stale
    int dominanceComparisons = 0;  ///< individual dominates()/isEquivalent() calls made
    int queuePushes = 0;
    int queuePops = 0;
    int maximumLabelsAtNode = 0;  ///< high-water mark across all nodes, updated as it grows
    int targetLabelCount = 0;     ///< size of the final non-dominated set at the target
    bool truncated = false;       ///< true if maxLabels (ParetoSearchOptions) was hit
    double executionTimeMs = 0.0;
};

}  // namespace orca
