#include "Orbit.hpp"

#include <cmath>
#include <format>

#include "../core/Constants.hpp"
#include "../core/Math.hpp"
#include "../core/Validation.hpp"
#include "SimplifiedOrbitalModel.hpp"

namespace orca {

namespace som = simplified_orbital_model;

Orbit::Orbit(Kilometres semiMajorAxisKm,
             Radians inclinationRad,
             Radians raanRad,
             Radians phaseAtEpochRad) noexcept
    : semiMajorAxisKm_(semiMajorAxisKm),
      inclinationRad_(inclinationRad),
      raanRad_(raanRad),
      phaseAtEpochRad_(phaseAtEpochRad) {}

Orbit Orbit::fromRadius(Kilometres semiMajorAxisKm,
                        Radians inclinationRad,
                        Radians raanRad,
                        Radians phaseAtEpochRad) {
    requireThat(std::isfinite(semiMajorAxisKm),
                "Orbit: semiMajorAxisKm must be finite");
    requireThat(std::isfinite(inclinationRad) && std::isfinite(raanRad) &&
                    std::isfinite(phaseAtEpochRad),
                "Orbit: all angles must be finite");

    // An orbit at or below the surface is not an orbit. This catches the common scenario
    // authoring error of supplying an altitude where a radius is expected.
    requireThat(semiMajorAxisKm > kEarthRadiusKm,
                std::format("Orbit: semiMajorAxisKm ({:.3f}) must exceed Earth's radius "
                            "({:.3f}); did you mean fromAltitude()?",
                            semiMajorAxisKm, kEarthRadiusKm));

    requireThat(inclinationRad >= 0.0 && inclinationRad <= kPi,
                std::format("Orbit: inclinationRad ({:.6f}) must lie in [0, pi]",
                            inclinationRad));

    // RAAN and phase are cyclic, so normalise rather than reject.
    return Orbit{semiMajorAxisKm, inclinationRad, normalizeAngle(raanRad),
                 normalizeAngle(phaseAtEpochRad)};
}

Orbit Orbit::fromAltitude(Kilometres altitudeKm,
                          Radians inclinationRad,
                          Radians raanRad,
                          Radians phaseAtEpochRad) {
    requireThat(std::isfinite(altitudeKm), "Orbit: altitudeKm must be finite");
    requireThat(altitudeKm > 0.0,
                std::format("Orbit: altitudeKm ({:.3f}) must be positive", altitudeKm));
    return fromRadius(radiusFromAltitudeKm(altitudeKm), inclinationRad, raanRad,
                      phaseAtEpochRad);
}

Kilometres Orbit::altitudeKm() const noexcept {
    return altitudeFromRadiusKm(semiMajorAxisKm_);
}

Seconds Orbit::periodSec() const noexcept {
    return som::orbitalPeriodSec(semiMajorAxisKm_);
}

double Orbit::meanMotionRadPerSec() const noexcept {
    return som::meanMotionRadPerSec(semiMajorAxisKm_);
}

KmPerSec Orbit::speedKmPerSec() const noexcept {
    return som::circularSpeedKmPerSec(semiMajorAxisKm_);
}

OrbitBand Orbit::band() const noexcept {
    return som::classifyBand(semiMajorAxisKm_);
}

Radians Orbit::phaseAtRad(Seconds timeSec) const noexcept {
    return som::phaseAtRad(phaseAtEpochRad_, semiMajorAxisKm_, timeSec);
}

Vector3 Orbit::positionAtKm(Seconds timeSec) const noexcept {
    return som::positionKm(semiMajorAxisKm_, inclinationRad_, raanRad_,
                           phaseAtRad(timeSec));
}

}  // namespace orca
