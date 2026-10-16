#pragma once

/// @file PathResult.hpp
/// The outcome of a shortest-path search: a route, or a documented absence of one.
///
/// ## Named factories, not a boolean-and-hope
///
/// `PathResult::found()` and `PathResult::notFound()` mirror the pattern Phase 1's
/// `MissionPlan` established for the same reason: reaching the "not found" state should
/// not require also supplying a (meaningless) route, and reaching the "found" state should
/// not be possible without supplying one. The invariant "found implies non-empty legs" is
/// therefore structural, not merely asserted.
///
/// ## `PathLeg` carries the *actual* evaluation, not the edge's cache
///
/// Each leg stores the `TransferResult` Dijkstra computed at that leg's real departure
/// time — not `GraphEdge::referenceResult()`. This is what lets a caller trust
/// `totalObjectives()`: it is built by composing what actually happened along this
/// specific, time-accurate route, using the same `compose()` rule the rest of ORCA uses
/// (ObjectiveVector.hpp), never a hand-rolled summation.

#include <vector>

#include "../core/ObjectiveVector.hpp"
#include "../core/Types.hpp"
#include "../transfer/TransferTypes.hpp"
#include "SearchMetrics.hpp"

namespace orca {

struct PathLeg {
    EdgeId edge;
    NodeId from;
    NodeId to;
    TransferResult result;  ///< evaluated at this leg's actual departure time
};

class PathResult {
public:
    [[nodiscard]] static PathResult notFound(NodeId source, NodeId target,
                                             Seconds startTimeSec, SearchMetrics metrics);

    /// @throws std::invalid_argument if `legs` is empty — a found path with zero legs is
    ///         the trivial `source == target` case, which is not represented as "found
    ///         with no legs" but is instead handled by the caller before search begins
    ///         (Dijkstra.cpp). A `found()` result always has at least one leg.
    [[nodiscard]] static PathResult found(NodeId source, NodeId target, Seconds startTimeSec,
                                          std::vector<PathLeg> legs, SearchMetrics metrics);

    /// The trivial zero-leg path: source and target are the same node, reachable at the
    /// requested start time with no cost. Kept distinct from `found()`'s general case so
    /// that "found implies non-empty legs" remains a true structural invariant elsewhere.
    [[nodiscard]] static PathResult trivial(NodeId sourceAndTarget, Seconds startTimeSec,
                                            SearchMetrics metrics);

    [[nodiscard]] bool isFound() const noexcept { return found_; }
    [[nodiscard]] NodeId source() const noexcept { return source_; }
    [[nodiscard]] NodeId target() const noexcept { return target_; }
    [[nodiscard]] Seconds startTimeSec() const noexcept { return startTimeSec_; }
    [[nodiscard]] const std::vector<PathLeg>& legs() const noexcept { return legs_; }
    [[nodiscard]] const SearchMetrics& metrics() const noexcept { return metrics_; }

    /// Arrival time at `target`. Equal to `startTimeSec()` for the trivial path.
    ///
    /// @throws std::invalid_argument if `!isFound()`.
    [[nodiscard]] Seconds arrivalTimeSec() const;

    /// Objectives accumulated over the whole route via `compose()` — additive for time,
    /// fuel and distance; independent-survival for risk. Zero vector for the trivial path.
    ///
    /// @throws std::invalid_argument if `!isFound()`.
    [[nodiscard]] const ObjectiveVector& totalObjectives() const;

    /// `[source, ..., target]`. Single-element (just `source`) for the trivial path.
    ///
    /// @throws std::invalid_argument if `!isFound()`.
    [[nodiscard]] std::vector<NodeId> nodeSequence() const;

private:
    PathResult(bool found, NodeId source, NodeId target, Seconds startTimeSec,
              std::vector<PathLeg> legs, ObjectiveVector totalObjectives,
              SearchMetrics metrics) noexcept;

    bool found_;
    NodeId source_;
    NodeId target_;
    Seconds startTimeSec_;
    std::vector<PathLeg> legs_;
    ObjectiveVector totalObjectives_;
    SearchMetrics metrics_;
};

}  // namespace orca
