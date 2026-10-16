#pragma once

/// @file GraphEdge.hpp
/// A directed, feasible transfer between two graph nodes.
///
/// ## What "feasible" means here, precisely
///
/// An edge exists because `TransferModel::evaluate()` returned `feasible == true` under
/// the **build-time reference constraints** (GraphBuilder.hpp): capability and timing
/// unbounded, only the structural and per-leg-policy checks live — `InvalidOrbit`,
/// "no rendezvous window ever" and the caller's risk/distance ceilings. An edge's
/// existence is therefore a fact about the two orbits alone, not about any specific
/// vehicle's fuel or any specific departure time.
///
/// ## `referenceResult` is a cached sample, not the truth
///
/// GraphEdge stores the `TransferResult` computed once at graph-build time, at a fixed
/// reference departure time. This mirrors DATA_MODEL.md §4's original design
/// (`baseCost` as "a cached sample, not the truth").
///
/// It exists for statistics and future visualisation (SYSTEM_DESIGN.md §11) — it is
/// **never** read by the search. Dijkstra re-evaluates every edge at the actual arrival
/// time of the node it is being relaxed from (GRAPH_MODEL.md §6), because `waitSec` and
/// `arrivalTimeSec` are genuinely time-dependent (PhasingModel.hpp) even though `deltaVKms`,
/// `fuelCost`, `risk` and `pathLengthKm` are not (TransferModel.hpp's second property).
/// Reading the cache from the search would silently reintroduce the "one global departure
/// time" bug Phase 3's brief names as its critical rule.

#include "../core/Types.hpp"
#include "../core/Units.hpp"
#include "../transfer/TransferTypes.hpp"

namespace orca {

class GraphEdge {
public:
    GraphEdge(EdgeId id,
             NodeId from,
             NodeId to,
             TransferResult referenceResult,
             Seconds referenceDepartureTimeSec) noexcept
        : id_(id),
          from_(from),
          to_(to),
          referenceResult_(referenceResult),
          referenceDepartureTimeSec_(referenceDepartureTimeSec) {}

    [[nodiscard]] EdgeId id() const noexcept { return id_; }
    [[nodiscard]] NodeId from() const noexcept { return from_; }
    [[nodiscard]] NodeId to() const noexcept { return to_; }

    /// The build-time sample. See the file comment: never authoritative for search.
    [[nodiscard]] const TransferResult& referenceResult() const noexcept {
        return referenceResult_;
    }

    [[nodiscard]] Seconds referenceDepartureTimeSec() const noexcept {
        return referenceDepartureTimeSec_;
    }

private:
    EdgeId id_;
    NodeId from_;
    NodeId to_;
    TransferResult referenceResult_;
    Seconds referenceDepartureTimeSec_;
};

}  // namespace orca
