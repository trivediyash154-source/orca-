#include "HohmannTransfer.hpp"

#include <cassert>
#include <cmath>

#include "../core/Constants.hpp"
#include "PlaneChange.hpp"

namespace orca::hohmann {

namespace {

/// Speed on an ellipse of semi-major axis `a` at radius `r`, from the vis-viva equation:
///     v^2 = mu * (2/r - 1/a)
[[nodiscard]] KmPerSec visViva(Kilometres radiusKm, Kilometres semiMajorAxisKm) noexcept {
    const double squared = kMuEarth * (2.0 / radiusKm - 1.0 / semiMajorAxisKm);
    return squared > 0.0 ? std::sqrt(squared) : 0.0;
}

[[nodiscard]] KmPerSec circularSpeed(Kilometres radiusKm) noexcept {
    return std::sqrt(kMuEarth / radiusKm);
}

}  // namespace

TransferEllipse compute(Kilometres sourceRadiusKm,
                        Kilometres targetRadiusKm,
                        Radians deltaInclinationRad) noexcept {
    assert(sourceRadiusKm > 0.0 && targetRadiusKm > 0.0);

    TransferEllipse e;
    e.semiMajorAxisKm = 0.5 * (sourceRadiusKm + targetRadiusKm);

    e.speedCircularAtSourceKms = circularSpeed(sourceRadiusKm);
    e.speedCircularAtTargetKms = circularSpeed(targetRadiusKm);
    e.speedOnTransferAtSourceKms = visViva(sourceRadiusKm, e.semiMajorAxisKm);
    e.speedOnTransferAtTargetKms = visViva(targetRadiusKm, e.semiMajorAxisKm);

    // Coplanar burn magnitudes, kept so the plane-change share can be isolated below.
    const KmPerSec coplanarDeparture =
        std::fabs(e.speedOnTransferAtSourceKms - e.speedCircularAtSourceKms);
    const KmPerSec coplanarArrival =
        std::fabs(e.speedOnTransferAtTargetKms - e.speedCircularAtTargetKms);

    // Combine the plane change into the burn at the larger radius, where the spacecraft is
    // slowest and rotating the velocity vector is cheapest.
    if (targetRadiusKm >= sourceRadiusKm) {
        e.departureDeltaVKms = coplanarDeparture;
        e.arrivalDeltaVKms = plane_change::combinedDeltaVKms(
            e.speedOnTransferAtTargetKms, e.speedCircularAtTargetKms, deltaInclinationRad);
    } else {
        e.departureDeltaVKms = plane_change::combinedDeltaVKms(
            e.speedCircularAtSourceKms, e.speedOnTransferAtSourceKms, deltaInclinationRad);
        e.arrivalDeltaVKms = coplanarArrival;
    }

    e.totalDeltaVKms = e.departureDeltaVKms + e.arrivalDeltaVKms;

    // The inclination's share is the extra cost over the same transfer flown coplanar.
    // Non-negative because the combined burn is never cheaper than the coplanar one.
    e.planeChangeDeltaVKms = e.totalDeltaVKms - (coplanarDeparture + coplanarArrival);
    if (e.planeChangeDeltaVKms < 0.0) {
        e.planeChangeDeltaVKms = 0.0;  // guards float noise at di == 0
    }

    // Half the period of the transfer ellipse: one burn to the other.
    e.coastTimeSec = kPi * std::sqrt((e.semiMajorAxisKm * e.semiMajorAxisKm *
                                      e.semiMajorAxisKm) / kMuEarth);

    e.pathLengthKm = transferArcLengthKm(sourceRadiusKm, targetRadiusKm);

    return e;
}

KmPerSec sequentialDeltaVKms(Kilometres sourceRadiusKm,
                             Kilometres targetRadiusKm,
                             Radians deltaInclinationRad) noexcept {
    assert(sourceRadiusKm > 0.0 && targetRadiusKm > 0.0);

    const Kilometres a = 0.5 * (sourceRadiusKm + targetRadiusKm);
    const KmPerSec departure =
        std::fabs(visViva(sourceRadiusKm, a) - circularSpeed(sourceRadiusKm));
    const KmPerSec arrival =
        std::fabs(visViva(targetRadiusKm, a) - circularSpeed(targetRadiusKm));

    // Third burn: pure plane change at the higher circular orbit, where it is cheapest to
    // do separately.
    const Kilometres higher = std::fmax(sourceRadiusKm, targetRadiusKm);
    const KmPerSec planeChange =
        plane_change::pureDeltaVKms(circularSpeed(higher), deltaInclinationRad);

    return departure + arrival + planeChange;
}

Kilometres transferArcLengthKm(Kilometres sourceRadiusKm,
                               Kilometres targetRadiusKm) noexcept {
    assert(sourceRadiusKm > 0.0 && targetRadiusKm > 0.0);

    // Transfer ellipse: periapsis and apoapsis are the two orbital radii, so
    //   a = (r1 + r2) / 2      and      b = sqrt(r1 * r2)
    const double a = 0.5 * (sourceRadiusKm + targetRadiusKm);
    const double b = std::sqrt(sourceRadiusKm * targetRadiusKm);

    // Ramanujan II. Exact for a circle (a == b), where it yields 2*pi*a.
    const double perimeter =
        kPi * (3.0 * (a + b) - std::sqrt((3.0 * a + b) * (a + 3.0 * b)));

    return 0.5 * perimeter;
}

}  // namespace orca::hohmann
