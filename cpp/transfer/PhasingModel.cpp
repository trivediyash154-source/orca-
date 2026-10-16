#include "PhasingModel.hpp"

#include <cmath>
#include <limits>

#include "../core/Constants.hpp"
#include "../core/Math.hpp"

namespace orca::phasing {

Seconds synodicPeriodSec(const Orbit& a, const Orbit& b) noexcept {
    const double drift = b.meanMotionRadPerSec() - a.meanMotionRadPerSec();
    if (std::fabs(drift) < kMinRelativeDriftRadPerSec) {
        return std::numeric_limits<double>::infinity();
    }
    return kTwoPi / std::fabs(drift);
}

RendezvousWindow nextWindow(const Orbit& source,
                            const Orbit& target,
                            Seconds departureTimeSec,
                            Seconds coastSec,
                            Radians transferArcRad) noexcept {
    RendezvousWindow window;

    const double omegaTarget = target.meanMotionRadPerSec();
    const double drift = omegaTarget - source.meanMotionRadPerSec();

    // Required lead angle: by the time we have flown `transferArcRad` in `coastSec`, the
    // target must have drifted into the arrival point. So at departure it must lead us by
    // the arc minus however far it will travel during the coast.
    window.requiredLeadRad = normalizeAngle(transferArcRad - omegaTarget * coastSec);

    // Relative phase now. This is where the RAAN simplification lives: both phases are
    // treated as angles in one shared reference (see the header).
    window.currentRelativePhaseRad = normalizeAngle(target.phaseAtRad(departureTimeSec) -
                                                    source.phaseAtRad(departureTimeSec));

    if (std::fabs(drift) < kMinRelativeDriftRadPerSec) {
        // No relative motion: the geometry never changes. Either it is already correct,
        // or waiting will never make it correct. Two spacecraft in the same orbit at
        // different phases fall here, and that is physically right -- catching up requires
        // changing orbit, which this model does not offer.
        window.synodicPeriodSec = std::numeric_limits<double>::infinity();
        const double offset = angularSeparation(window.currentRelativePhaseRad,
                                                window.requiredLeadRad);
        window.exists = offset <= kAlignmentToleranceRad;
        window.waitSec = 0.0;
        window.windowTimeSec = departureTimeSec;
        return window;
    }

    window.synodicPeriodSec = kTwoPi / std::fabs(drift);

    // Angular gap still to be closed, measured in the direction the relative phase is
    // actually drifting. Using normalizeAngle keeps this in [0, 2*pi), so the gap shrinks
    // linearly to zero and then wraps to a full revolution -- which is exactly the
    // staircase that makes arrival time non-decreasing.
    const double gapRad = drift > 0.0
                              ? normalizeAngle(window.requiredLeadRad -
                                               window.currentRelativePhaseRad)
                              : normalizeAngle(window.currentRelativePhaseRad -
                                               window.requiredLeadRad);

    window.waitSec = gapRad / std::fabs(drift);
    window.windowTimeSec = departureTimeSec + window.waitSec;
    window.exists = true;
    return window;
}

}  // namespace orca::phasing
