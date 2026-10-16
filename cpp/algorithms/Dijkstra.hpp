#pragma once

/// @file Dijkstra.hpp
/// Earliest-arrival shortest path over a time-dependent `MissionGraph`.
///
/// # THE CRITICAL RULE
///
/// Every outgoing edge is evaluated at the arrival time of the node being expanded — never
/// at the search's global start time. Reaching an intermediate node at time `t` changes the
/// departure time of every edge leaving it. Getting this wrong produces a graph search that
/// silently behaves like a static-weight shortest path, which is wrong the moment any edge
/// on the optimal route has a non-zero wait (GRAPH_MODEL.md §6 has the dedicated test for
/// exactly this: `TimeDependence.IndirectRouteUsesArrivalTimeAtIntermediateNode`).
///
/// # WHAT THIS ALGORITHM OPTIMISES, AND WHAT IT DOES NOT ENFORCE
///
/// The priority key is **earliest arrival time**, full stop. This is deliberately not
/// multi-objective — that is Pareto label-setting's job (ALGORITHMS.md §5), which exists
/// precisely because a single scalar objective cannot express the time/fuel/risk trade-off
/// (OPTIMIZATION_MODEL.md §1.1).
///
/// One consequence of staying single-objective is worth stating precisely, because it is
/// easy to get wrong in a way that *looks* like a small addition: **this search enforces
/// only per-edge (single-leg) delta-v feasibility, not a cumulative path-wide delta-v
/// budget.**
///
/// A per-edge check — "is this one leg, by itself, within the vehicle's total budget" — is
/// sound to fold into single-label Dijkstra, because it is a fact about the edge alone,
/// independent of which path reached it. A genuinely *cumulative* check — "does the sum of
/// every leg's delta-v so far, plus this one, exceed the budget" — is **not** safe to fold
/// into single-label Dijkstra: a slower path that happens to have spent less fuel so far
/// could be wrongly pruned in favour of a faster-but-thirstier path that later turns out to
/// be the only one still affordable, and single-label search has already discarded it.
/// Enforcing a cumulative resource constraint correctly requires keeping multiple
/// non-dominated labels per node — which is exactly what Pareto label-setting already does.
/// Implementing a second, narrower version of it here would duplicate that machinery for a
/// case Phase 5 handles correctly and generally.
///
/// So: `findEarliestArrival` returns the true earliest-arrival path subject to every
/// *per-edge* constraint (delta-v, fuel, wait horizon, deadline), and reports that path's
/// total accumulated objectives for display — but a returned "found" result is not a
/// guarantee that the vehicle's *cumulative* delta-v across the whole route stays within
/// budget. `DijkstraTest.CumulativeBudgetIsNotEnforcedByDesign` exercises this directly so
/// the limitation is a tested boundary, not an undiscovered gap.
///
/// # Complexity
///
/// Structurally `O((V + E) log V)` with a binary heap and lazy deletion (ALGORITHMS.md
/// §2.4, §3.5) — but every edge relaxation calls `TransferModel::evaluate()`, so the
/// practical cost is `O((V + E) log V * C)` where `C` is one transfer evaluation (closed-form
/// trigonometry, no iteration — cheap, but not free, and not amortisable away as the graph
/// size grows the way a cached-weight Dijkstra's would be).

#include "../graph/MissionGraph.hpp"
#include "../transfer/TransferModel.hpp"
#include "PathResult.hpp"

namespace orca {

/// Vehicle capability and search policy, shared verbatim between Dijkstra and A*
/// (Phase 4): both algorithms build the same `TransferConstraints` from the same five
/// fields, so a single named type is the honest reflection of that, rather than a
/// Dijkstra-specific name A* would awkwardly reuse. GRAPH_MODEL.md §6 has the reasoning
/// this type embodies (why total budget rather than a decremented remaining balance, why
/// risk/distance are unbounded here).
struct SearchConstraints {
    /// The searching vehicle's total delta-v capability. Used as both `maxDeltaVKms` and
    /// `deltaVBudgetKms` for every relaxation, uniformly — see the file comment for why
    /// this is the total budget and not a manually-decremented remaining balance.
    KmPerSec vehicleDeltaVBudgetKms = 2.5;

    /// Longest acceptable loiter before a rendezvous window, re-checked at each edge's
    /// actual departure time (genuinely time-dependent — PhasingModel.hpp).
    Seconds maxWaitSec = 21600.0;

    /// Latest acceptable arrival at any node along the route. Defaults to "no deadline".
    Seconds latestArrivalSec = 1.0e18;

    /// Optional additional per-leg ceilings, tighter than whatever the graph was built
    /// with. Defaulted unbounded because risk/distance are Group 1 checks
    /// (GraphBuilder.hpp) already decided permanently by the fact an edge exists.
    double maxRisk = 1.0;
    Kilometres maxPathLengthKm = 1.0e9;
};

/// Find the earliest-arrival path from `source` to `target`, departing no earlier than
/// `startTimeSec`.
///
/// @throws std::invalid_argument if `source` or `target` is not a node in `graph`.
[[nodiscard]] PathResult findEarliestArrival(const MissionGraph& graph,
                                             const TransferModel& transferModel,
                                             NodeId source,
                                             NodeId target,
                                             Seconds startTimeSec,
                                             const SearchConstraints& constraints = {});

}  // namespace orca
