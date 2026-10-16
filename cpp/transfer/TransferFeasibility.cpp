#include "TransferFeasibility.hpp"

#include "../core/Constants.hpp"

namespace orca {

TransferFailureReason checkFeasibility(const TransferGeometry& geometry,
                                       Fraction fuelCost,
                                       double risk,
                                       const TransferConstraints& constraints) noexcept {
    // 1. Physical validity. Nothing below is meaningful without it.
    if (!geometry.orbitsValid) {
        return TransferFailureReason::InvalidOrbit;
    }

    // 2. Reachability. Reported before affordability because no amount of fuel fixes a
    //    rendezvous geometry that never occurs.
    if (!geometry.rendezvousWindowExists) {
        return TransferFailureReason::PhasingUnavailable;
    }
    if (geometry.waitSec > constraints.maxWaitSec) {
        return TransferFailureReason::PhasingUnavailable;
    }

    // 3. Capability. Delta-v and fuel are the same constraint expressed in two units, so
    //    both are checked: a caller may set either, and they can disagree when the vehicle
    //    is partly depleted.
    //
    //    The epsilon slack matters at the boundary: a transfer requiring exactly the
    //    remaining budget must be allowed, and floating-point arithmetic on two
    //    independently computed burn magnitudes will not land exactly on it.
    if (geometry.deltaVKms > constraints.maxDeltaVKms + kEpsilon) {
        return TransferFailureReason::DeltaVExceeded;
    }
    if (fuelCost > constraints.availableFuelFraction + kEpsilon) {
        return TransferFailureReason::FuelInsufficient;
    }

    // 4. Timing against the caller's deadline (a casualty's survival window, typically).
    if (geometry.arrivalTimeSec > constraints.latestArrivalSec) {
        return TransferFailureReason::DeadlineExceeded;
    }

    // 5. Policy limits.
    if (geometry.pathLengthKm > constraints.maxPathLengthKm) {
        return TransferFailureReason::DistanceExceeded;
    }
    if (risk > constraints.maxRisk + kEpsilon) {
        return TransferFailureReason::RiskExceeded;
    }

    return TransferFailureReason::Feasible;
}

}  // namespace orca
