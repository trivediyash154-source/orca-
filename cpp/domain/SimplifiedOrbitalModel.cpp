#include "SimplifiedOrbitalModel.hpp"

#include <cassert>
#include <cmath>

#include "../core/Math.hpp"

namespace orca::simplified_orbital_model {

Seconds orbitalPeriodSec(Kilometres radiusKm) noexcept {
    assert(radiusKm > 0.0 && "orbital period requires a positive radius");
    return kTwoPi * std::sqrt((radiusKm * radiusKm * radiusKm) / kMuEarth);
}

double meanMotionRadPerSec(Kilometres radiusKm) noexcept {
    assert(radiusKm > 0.0 && "mean motion requires a positive radius");
    return std::sqrt(kMuEarth / (radiusKm * radiusKm * radiusKm));
}

KmPerSec circularSpeedKmPerSec(Kilometres radiusKm) noexcept {
    assert(radiusKm > 0.0 && "circular speed requires a positive radius");
    return std::sqrt(kMuEarth / radiusKm);
}

Radians phaseAtRad(Radians phaseAtEpochRad, Kilometres radiusKm, Seconds timeSec) noexcept {
    return normalizeAngle(phaseAtEpochRad + meanMotionRadPerSec(radiusKm) * timeSec);
}

Vector3 positionKm(Kilometres radiusKm,
                   Radians inclinationRad,
                   Radians raanRad,
                   Radians phaseRad) noexcept {
    const double cosRaan = std::cos(raanRad);
    const double sinRaan = std::sin(raanRad);
    const double cosPhase = std::cos(phaseRad);
    const double sinPhase = std::sin(phaseRad);
    const double cosIncl = std::cos(inclinationRad);
    const double sinIncl = std::sin(inclinationRad);

    return Vector3{
        radiusKm * (cosRaan * cosPhase - sinRaan * sinPhase * cosIncl),
        radiusKm * (sinRaan * cosPhase + cosRaan * sinPhase * cosIncl),
        radiusKm * (sinPhase * sinIncl),
    };
}

OrbitBand classifyBand(Kilometres radiusKm) noexcept {
    const Kilometres altitude = altitudeFromRadiusKm(radiusKm);
    if (altitude < kLeoUpperAltitudeKm) {
        return OrbitBand::Leo;
    }
    if (altitude < kMeoUpperAltitudeKm) {
        return OrbitBand::Meo;
    }
    return OrbitBand::Geo;
}

}  // namespace orca::simplified_orbital_model
