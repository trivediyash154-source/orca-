#include "PlaneChange.hpp"

#include <cassert>
#include <cmath>

#include "../core/Constants.hpp"

namespace orca::plane_change {

KmPerSec pureDeltaVKms(KmPerSec speedKms, Radians deltaInclinationRad) noexcept {
    assert(speedKms >= 0.0 && "plane change requires a non-negative speed");
    return 2.0 * speedKms * std::sin(std::fabs(deltaInclinationRad) * 0.5);
}

KmPerSec combinedDeltaVKms(KmPerSec speedBeforeKms,
                           KmPerSec speedAfterKms,
                           Radians deltaInclinationRad) noexcept {
    assert(speedBeforeKms >= 0.0 && speedAfterKms >= 0.0);

    const double di = std::fabs(deltaInclinationRad);
    const double squared = speedBeforeKms * speedBeforeKms + speedAfterKms * speedAfterKms -
                           2.0 * speedBeforeKms * speedAfterKms * std::cos(di);

    // The law of cosines cannot produce a negative value mathematically, but catastrophic
    // cancellation can when v1 ~= v2 and di ~= 0 -- exactly the coplanar same-altitude
    // case ORCA evaluates constantly. Clamping at zero keeps sqrt well-defined instead of
    // propagating a NaN into every downstream objective.
    return squared > 0.0 ? std::sqrt(squared) : 0.0;
}

Radians inclinationDifferenceRad(Radians a, Radians b) noexcept {
    return std::fabs(a - b);
}

}  // namespace orca::plane_change
