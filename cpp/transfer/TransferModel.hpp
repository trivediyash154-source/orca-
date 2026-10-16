#pragma once

/// @file TransferModel.hpp
/// The transfer evaluator — the one interface Phase 3's graph builder needs.
///
/// ## The contract
///
///     TransferResult evaluate(source, target, departureTimeSec, constraints)
///
/// The graph builder calls this and gets back a four-objective cost vector plus a
/// feasibility verdict. It never needs to know that Hohmann transfers, combined plane
/// changes or synodic periods exist. That opacity is the point: swapping in a Lambert
/// solver later (RESEARCH_EXTENSION.md §3) changes this file and nothing above it.
///
/// ## Why the signature takes a departure time
///
/// Because the answer depends on it. The transfer geometry is fixed by the two orbits, but
/// *when a rendezvous is possible* depends on where the two bodies are, which depends on
/// time. Hence `w(edge, t_departure)` rather than `w(edge)`.
///
/// ## The two properties Phase 3 relies on
///
/// **1. FIFO.** For `t1 <= t2`, `arrival(t1) <= arrival(t2)`. Departing later never
/// arrives earlier. This is what makes Dijkstra's greedy commitment sound on a
/// time-dependent graph. It holds *by construction* here, not by tuning: see
/// PhasingModel.hpp for the proof.
///
/// **2. Time-independent non-time objectives.** `deltaVKms`, `fuelCost`, `risk` and
/// `pathLengthKm` are functions of the two orbits alone — they do not vary with departure
/// time. Only `transferSec` does.
///
/// The second property is stronger than FIFO and is easy to overlook. Multi-objective
/// label-setting prunes a label when another dominates it, and that pruning is sound only
/// if extending the dominating label is never worse. On a time-dependent graph where
/// *fuel* also varied with departure time, an earlier-but-cheaper label could extend to a
/// more expensive edge than a later one, and dominance pruning would silently discard
/// optimal routes. Absorbing all phase dependence into wait time is what avoids that.
///
/// Both properties are asserted by the test suite over thousands of randomised orbit
/// pairs, not merely argued here.
///
/// ## What this class is not
///
/// It does not build graphs, search, schedule, or know what a mission is. It answers one
/// question about one ordered pair of orbits at one instant.

#include "TransferRiskModel.hpp"
#include "TransferTypes.hpp"

namespace orca {

class TransferModel {
public:
    /// Construct with the risk weights to use. Defaults are documented in
    /// TransferRiskWeights.
    explicit TransferModel(TransferRiskWeights riskWeights = {}) noexcept;

    /// Evaluate a transfer.
    ///
    /// Returns a result whose numeric fields are **always populated** when the orbits are
    /// valid, feasible or not, so callers can report *how far past* a limit a rejected
    /// transfer fell. No sentinel values are used to indicate failure.
    [[nodiscard]] TransferResult evaluate(const Orbit& source,
                                          const Orbit& target,
                                          Seconds departureTimeSec,
                                          const TransferConstraints& constraints) const noexcept;

    /// Convenience overload for callers holding a bundled request.
    [[nodiscard]] TransferResult evaluate(const TransferRequest& request) const noexcept;

    /// Compute the physical geometry only, skipping cost and feasibility.
    ///
    /// Exposed because the FIFO tests need arrival times for transfers that no vehicle
    /// could afford — FIFO is a property of the *geometry*, and testing it only on
    /// affordable transfers would leave most of the domain unchecked.
    [[nodiscard]] TransferGeometry computeGeometry(const Orbit& source,
                                                   const Orbit& target,
                                                   Seconds departureTimeSec) const noexcept;

    /// Radius difference below which two orbits are treated as one, making the manoeuvre a
    /// pure plane change with no coast time.
    ///
    /// One metre. Chosen to be far below any altitude difference a scenario would express,
    /// while still catching the exactly-equal case that arises when a vehicle is asked to
    /// transfer to its own orbit.
    static constexpr Kilometres kSameRadiusToleranceKm = 1.0e-3;

private:
    TransferRiskWeights riskWeights_;
};

}  // namespace orca
