#pragma once

/// @file GraphBuilder.hpp
/// Turns a scenario's satellites and vehicles into a `MissionGraph`.
///
/// # THE THREE-WAY FEASIBILITY SPLIT — READ THIS BEFORE CHANGING BUILD OR SEARCH LOGIC
///
/// `TransferModel`'s two Phase 2 properties (FIFO; non-time objectives independent of
/// departure time — TransferModel.hpp) mean the seven `TransferFailureReason`s split
/// cleanly into three groups with three different lifetimes. Getting this split wrong is
/// the single easiest way to reintroduce a correctness bug in this subsystem, so it is
/// stated once, here, and referenced rather than repeated elsewhere.
///
/// **Group 1 — time-independent AND vehicle-independent. Decided once, forever, by the
/// builder:**
///   - `InvalidOrbit`
///   - `PhasingUnavailable`, specifically the *"no relative drift and not aligned"* case
///     — a permanent geometric fact about the pair, true at every departure time
///     (PhasingModel.hpp's proof that a zero-drift relative phase never changes).
///   - `RiskExceeded`, `DistanceExceeded` — a policy ceiling on a single leg, and a single
///     leg's risk/distance do not depend on when you fly it.
///
///   These are exactly the reasons a build using **unbounded capability and unbounded
///   wait/deadline** can produce. That is what this builder does: it evaluates every
///   candidate pair with delta-v effectively unlimited and the wait horizon effectively
///   infinite, so the *only* rejections it can ever record are from this group. An edge
///   that exists in the graph is therefore a permanent fact — true for every vehicle, at
///   every departure time.
///
/// **Group 2 — time-independent but vehicle-dependent. Decided once per search, by
/// Dijkstra, per edge, using that vehicle's own budget:**
///   - `DeltaVExceeded`, `FuelInsufficient` — whether *this one leg alone* is affordable
///     depends on which vehicle is asking, but not on when it asks.
///
///   The builder cannot decide these because it does not know which vehicle will use a
///   given edge — the same edge is valid graph structure for every vehicle, affordable or
///   not. Baking a specific vehicle's budget into the graph would make the graph
///   single-vehicle and require rebuilding it per search, which is unnecessary: the check
///   is cheap and belongs where the vehicle is known.
///
/// **Group 3 — genuinely time-dependent. Decided at every relaxation, by Dijkstra, using
/// the arrival time at the predecessor:**
///   - `PhasingUnavailable`, the *"window exists but the wait exceeds the horizon"* case.
///   - `DeadlineExceeded`.
///
///   `waitSec` is a staircase in departure time (PhasingModel.hpp §8.2); there is no
///   single build-time answer to "is the wait acceptable" because the wait itself changes
///   with when you ask.
///
/// This is why `GraphEdge::referenceResult()` is a *sample*, and why Dijkstra never reads
/// it (GraphEdge.hpp) — it calls `TransferModel::evaluate()` fresh, every relaxation, with
/// the real vehicle constraints and the real current-node arrival time as the departure
/// time.
///
/// ## Complexity
///
/// `O(V^2)` candidate ordered pairs before any pruning, each evaluated by one
/// `TransferModel::evaluate()` call. No spatial pruning is implemented — that is a later
/// optimisation phase (IMPLEMENTATION_ROADMAP.md), and introducing one now would be
/// optimising a cost that has not yet been measured against a real scenario size.

#include "../domain/Scenario.hpp"
#include "../transfer/TransferModel.hpp"
#include "GraphBuildStats.hpp"
#include "MissionGraph.hpp"

namespace orca {

/// Options are declared at namespace scope, not nested inside `GraphBuilder`: a nested
/// class's default member initializers are not usable in a default *function* argument of
/// the enclosing class until the enclosing class is complete, which a `build(..., const
/// Options& = {})` declaration inside `GraphBuilder` itself is not yet. Namespace scope
/// sidesteps the ordering problem entirely.
struct GraphBuilderOptions {
    /// Per-leg risk ceiling. Time-independent (Group 1), so legitimately enforced
    /// once at build time regardless of which vehicle eventually flies the edge.
    double maxRisk = 1.0;

    /// Per-leg transfer-arc-length ceiling, km. Same status as `maxRisk`.
    Kilometres maxPathLengthKm = 1.0e9;

    /// The instant at which the cached `referenceResult` on each edge is evaluated.
    /// Affects only that cache's `waitSec`/`arrivalTimeSec` fields — never which edges
    /// exist, because Group 1 membership does not depend on this value. See
    /// `GraphBuilderTest.EdgeSetIsInvariantToReferenceTime` for the property this
    /// claim rests on.
    Seconds referenceTimeSec = 0.0;
};

struct GraphBuildOutput {
    MissionGraph graph;
    GraphBuildStats stats;
};

class GraphBuilder {
public:
    using Options = GraphBuilderOptions;
    using BuildOutput = GraphBuildOutput;

    /// Build a graph over every eligible satellite and vehicle in `scenario`.
    ///
    /// Eligible: satellites that are not in a terminal state (`Rescued`/`Lost` satellites
    /// have nothing left to stage through or rescue), and vehicles that are not
    /// `Disabled`. A candidate edge is attempted between every ordered pair of eligible
    /// nodes; this is deliberately unrestricted by node kind — a satellite-to-satellite
    /// edge represents staging through a healthy satellite's orbit (SYSTEM_DESIGN.md §3),
    /// and *which* node ultimately serves as a search source or sink is a planning
    /// decision this builder does not make (Phase 3 brief §10).
    ///
    /// `transferModel` is taken by reference, not owned: the caller supplies the same
    /// instance that will later re-evaluate edges during search, so there is exactly one
    /// `TransferModel` in play and no risk of the graph and the search disagreeing about
    /// risk weights.
    [[nodiscard]] static BuildOutput build(const Scenario& scenario,
                                           const TransferModel& transferModel,
                                           const Options& options = {});
};

}  // namespace orca
