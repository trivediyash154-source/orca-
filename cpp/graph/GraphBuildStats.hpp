#pragma once

/// @file GraphBuildStats.hpp
/// Rejection accounting for a graph build.
///
/// One field per `TransferFailureReason` — deliberately not a `map<TransferFailureReason,
/// int>`. Explicit fields mean the compiler flags a missed case if the enum ever gains a
/// member (the same reasoning `Types.hpp`'s `toString` switches use), and they mean a
/// reader can see the whole taxonomy by looking at the struct rather than by finding
/// everywhere a map key is inserted.
///
/// **`rejectedPhasingUnavailable` is never folded into a generic "infeasible" bucket.**
/// That was an explicit requirement of Phase 3 (§15 of the brief): phasing rejections are
/// the single most informative rejection category ORCA has, since a near-co-altitude pair
/// is a permanent geometric fact, not a tunable policy limit — collapsing it away would
/// hide exactly the finding TRANSFER_MODEL.md §8.5 documents.

#include <cassert>
#include <cstddef>

#include "../transfer/TransferTypes.hpp"

namespace orca {

struct GraphBuildStats {
    int candidatePairs = 0;
    int feasibleEdges = 0;

    int rejectedInvalidOrbit = 0;
    int rejectedDeltaVExceeded = 0;      ///< time-independent; zero at build time (see
                                         ///< GraphBuilder.hpp — capability is deliberately
                                         ///< unbounded during construction), kept for API
                                         ///< completeness should that policy ever change.
    int rejectedFuelInsufficient = 0;    ///< same status as above.
    int rejectedPhasingUnavailable = 0;  ///< the "no rendezvous window, ever" case only.
    int rejectedDistanceExceeded = 0;
    int rejectedRiskExceeded = 0;
    int rejectedDeadlineExceeded = 0;    ///< time-independent at build time; see above.

    double buildTimeMs = 0.0;

    /// Increment the field matching `reason`. `Feasible` is a programmer error to pass
    /// here — a feasible transfer is not a rejection — and is asserted against.
    void recordRejection(TransferFailureReason reason) noexcept {
        switch (reason) {
            case TransferFailureReason::InvalidOrbit:
                ++rejectedInvalidOrbit;
                return;
            case TransferFailureReason::DeltaVExceeded:
                ++rejectedDeltaVExceeded;
                return;
            case TransferFailureReason::FuelInsufficient:
                ++rejectedFuelInsufficient;
                return;
            case TransferFailureReason::PhasingUnavailable:
                ++rejectedPhasingUnavailable;
                return;
            case TransferFailureReason::DistanceExceeded:
                ++rejectedDistanceExceeded;
                return;
            case TransferFailureReason::RiskExceeded:
                ++rejectedRiskExceeded;
                return;
            case TransferFailureReason::DeadlineExceeded:
                ++rejectedDeadlineExceeded;
                return;
            case TransferFailureReason::Feasible:
                assert(false && "recordRejection called with a feasible result");
                return;
        }
    }

    /// Sum of every rejection field. Used to cross-check
    /// `candidatePairs == feasibleEdges + totalRejected()`.
    [[nodiscard]] int totalRejected() const noexcept {
        return rejectedInvalidOrbit + rejectedDeltaVExceeded + rejectedFuelInsufficient +
               rejectedPhasingUnavailable + rejectedDistanceExceeded + rejectedRiskExceeded +
               rejectedDeadlineExceeded;
    }
};

}  // namespace orca
