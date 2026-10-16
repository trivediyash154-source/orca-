#pragma once

/// @file ParetoSearch.hpp
/// Multi-objective earliest-arrival search: Martins' label-setting algorithm with
/// dominance pruning, over the same time-dependent `MissionGraph` Dijkstra and A* search.
///
/// # THE DEPARTURE FROM DIJKSTRA/A*
///
/// A node no longer has one best value — it has a **set of mutually non-dominated
/// labels**. A route that is slower but cheaper is neither better nor worse than a faster,
/// costlier one; both survive, because `ObjectiveVector`'s four dimensions do not reduce
/// to a single order (`core/ObjectiveVector.hpp`'s file comment: no `operator<` is
/// provided, deliberately). `Dominance.hpp` is the predicate that decides which labels are
/// safe to discard; this file is the search that uses it.
///
/// # WHAT IS SHARED WITH DIJKSTRA/A*, AND WHY
///
/// `SearchConstraints` (`Dijkstra.hpp`) is reused verbatim: this search builds the
/// identical `TransferConstraints` from the identical five fields, the same genuine
/// sharing Phase 4 established between Dijkstra and A*. `PathLeg` (`PathResult.hpp`) is
/// reused verbatim too — once a label is reconstructed into a route, it is structurally
/// identical to what Dijkstra/A* already produce (a sequence of real, time-accurate
/// `TransferResult`s); the only difference here is that *several* such routes can survive
/// to the target instead of one.
///
/// `Dominance` itself lives in this directory rather than `optimization/` — see
/// `Dominance.hpp`'s file comment for why moving it there would recreate the dependency
/// cycle `ObjectiveVector` was moved out of `optimization/` to avoid in Phase 1.
///
/// # WHAT THIS FILE DOES NOT KNOW ABOUT
///
/// No `MissionPlan`, `VehicleId`, `EmergencyId`, `PlanId`. `ARCHITECTURE.md`'s dependency
/// table states `algorithms/` has no knowledge of missions or emergencies, and that
/// property is what makes every algorithm here unit-testable on a hand-built toy graph.
/// `findParetoRoutes()` therefore returns `ParetoRoute`s addressed only by `NodeId`/
/// `EdgeId`, exactly as `findEarliestArrival`/`findEarliestArrivalAStar` do.
/// `optimization/ParetoFront.hpp` is where a caller who *does* have a vehicle and an
/// emergency in hand turns this raw result into `MissionPlan`-bearing `ParetoFront`
/// (`DATA_MODEL.md` §7) — a deliberate architectural boundary, not an oversight; see that
/// file's comment for the reasoning.
///
/// # CUMULATIVE DELTA-V IS A HARD CONSTRAINT, ENFORCED DURING SEARCH
///
/// Dijkstra and A* both document (and test) that they cannot soundly enforce a cumulative,
/// path-wide delta-v budget — only a per-edge one — because single-label search has
/// already discarded the information needed (`Dijkstra.hpp`'s file comment). Multi-label
/// search is exactly the structure that fixes this: each label carries its own
/// accumulated `cost.fuel` (verified equal to accumulated-delta-v-over-budget —
/// `ParetoLabel.hpp`), and a candidate whose accumulated `cost.fuel` would exceed `1.0` is
/// rejected before it can ever enter the Pareto frontier, regardless of how it compares to
/// other labels on any other objective. This is the concrete fix for the limitation Phase
/// 3/4 documented and left open.
///
/// # LABEL-SETTING, NOT LABEL-CORRECTING — AND WHY REOPENING NEVER APPLIES HERE
///
/// A*'s reopening (`AStar.hpp`) exists because its heuristic is admissible but not
/// unconditionally consistent, so a node's first-popped *single* value is not always
/// trustworthy. That reasoning does not transfer here, and is not reused: this search uses
/// **no heuristic at all** (Phase 5 brief §56 — multi-objective admissibility is a
/// different, harder problem than single-objective admissibility, and is out of this
/// phase's scope). Once a label is popped and expanded, it is never revisited — every
/// label the pool ever holds is either superseded (deactivated) by dominance *before* it
/// is ever popped, or expanded exactly once. `active` is therefore checked only to skip
/// labels superseded *while still sitting in the open queue* (the multi-label analogue of
/// Dijkstra's lazy deletion, not a reopening mechanism) — see `ParetoSearch.cpp` for the
/// termination argument this depends on.

#include <optional>
#include <vector>

#include "../core/Types.hpp"
#include "../graph/MissionGraph.hpp"
#include "../transfer/TransferModel.hpp"
#include "Dijkstra.hpp"  // SearchConstraints
#include "ParetoSearchMetrics.hpp"
#include "PathResult.hpp"  // PathLeg

namespace orca {

/// Search-time policy specific to Pareto search. `SearchConstraints` covers vehicle
/// capability and per-edge policy identically to Dijkstra/A*; this covers only what is
/// genuinely new here: bounding label growth and (optionally) trading exactness for size.
struct ParetoSearchOptions {
    /// Hard cap on labels ever inserted into the pool. MOSP is NP-hard (ALGORITHMS.md
    /// §5.1) — this is the documented, measured truncation mechanism (Phase 5 brief §39),
    /// not a silent one: hitting it sets `ParetoSearchResult::metrics.truncated`, and a
    /// truncated result must never be presented as a complete Pareto front.
    int maxLabels = 100000;

    /// `std::nullopt` (the default, used by every correctness test in this codebase) is
    /// **exact mode**: every dominance check uses `dominates()`. A value here switches
    /// every dominance check to `dominatesWithEpsilon()` with this epsilon —
    /// **approximate mode**, which may discard genuinely non-dominated labels within
    /// `epsilon` (`Dominance.hpp`). Never set implicitly; Phase 5 brief §12, §40.
    std::optional<double> epsilonDominance = std::nullopt;
};

/// One reconstructed non-dominated (or, in `ParetoSearchResult::dominated`,
/// once-competitive-but-superseded) route from source to target.
struct ParetoRoute {
    std::vector<PathLeg> legs;  ///< empty only for the trivial source == target route
    ObjectiveVector totalCost;
    Seconds arrivalTimeSec = 0.0;
};

struct ParetoSearchResult {
    NodeId source;
    NodeId target;
    Seconds startTimeSec = 0.0;

    /// The Pareto front reaching `target`: mutually non-dominated (property-tested, not
    /// merely produced — `pareto_search_test.cpp`). Empty if `target` is unreachable.
    std::vector<ParetoRoute> solutions;

    /// Routes that reached `target` at some point during the search but were later
    /// dominated or found equivalent to a member of `solutions`. Retained only for routes
    /// that reached `target` specifically (not every dominated label at every
    /// intermediate node) — kept for the same reason `DATA_MODEL.md` §7's `ParetoFront`
    /// keeps its own `dominated` set: a far more convincing chart, and the ability to
    /// answer "why wasn't that one chosen."
    std::vector<ParetoRoute> dominated;

    ParetoSearchMetrics metrics;

    [[nodiscard]] bool isEmpty() const noexcept { return solutions.empty(); }
};

/// Find every Pareto-optimal (non-dominated) route from `source` to `target`, departing no
/// earlier than `startTimeSec`.
///
/// @throws std::invalid_argument if `source` or `target` is not a node in `graph`.
[[nodiscard]] ParetoSearchResult findParetoRoutes(
    const MissionGraph& graph, const TransferModel& transferModel, NodeId source,
    NodeId target, Seconds startTimeSec, const SearchConstraints& constraints = {},
    const ParetoSearchOptions& options = {});

}  // namespace orca
