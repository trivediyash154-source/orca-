#include "AStarHeuristic.hpp"

#include <algorithm>
#include <cmath>

#include "../core/Constants.hpp"
#include "../transfer/HohmannTransfer.hpp"
#include "../transfer/TransferModel.hpp"

namespace orca::astar_heuristic {

namespace {

/// Hohmann half-period between two radii, matching `TransferModel`'s own pure-plane-
/// change special case exactly: coast is zero when the radii coincide, not the smooth
/// formula's non-zero limit as one radius approaches the other. Using the same tolerance
/// constant `TransferModel` itself uses (rather than a second, independently-chosen one)
/// is what keeps this heuristic's notion of "the same orbit" identical to the model's.
[[nodiscard]] Seconds coastSec(Kilometres ra, Kilometres rb) noexcept {
    if (std::fabs(ra - rb) < TransferModel::kSameRadiusToleranceKm) {
        return 0.0;
    }
    return hohmann::compute(ra, rb, 0.0).coastTimeSec;
}

}  // namespace

Breakdown computeBreakdown(const Orbit& current, const Orbit& goal) noexcept {
    const Kilometres r0 = current.semiMajorAxisKm();
    const Kilometres r2 = goal.semiMajorAxisKm();

    Breakdown b;
    b.directHohmannSec = coastSec(r0, r2);
    b.floorDetourSec = coastSec(r0, kEarthRadiusKm) + coastSec(kEarthRadiusKm, r2);
    b.floorDetourWasTighter = b.floorDetourSec < b.directHohmannSec;
    b.chosenSec = std::min(b.directHohmannSec, b.floorDetourSec);
    return b;
}

Seconds estimateRemainingTimeSec(const Orbit& current, const Orbit& goal) noexcept {
    return computeBreakdown(current, goal).chosenSec;
}

}  // namespace orca::astar_heuristic
