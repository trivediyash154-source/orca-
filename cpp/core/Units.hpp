#pragma once

/// @file Units.hpp
/// Authoritative statement of ORCA's unit conventions, mirroring DATA_MODEL.md §0.1.
///
/// ORCA has one rule about units: a field name that does not state its unit uses the
/// table below. Names carry an explicit suffix wherever ambiguity is plausible.
///
///   Quantity              Unit                        Suffix
///   --------------------  --------------------------  ---------------
///   Distance / radius     kilometres                   Km
///   Altitude              kilometres above kEarthRadiusKm
///   Time                  seconds since scenario epoch Sec
///   Velocity / delta-v    kilometres per second        Kms
///   Angle (internal)      radians                      Rad
///   Angle (boundary only) degrees                      Deg
///   Fuel                  fraction of a vehicle's own
///                         delta-v budget, [0,1]        Fraction
///   Risk                  dimensionless [0,1]          -
///
/// Two conventions that are easy to get wrong and are therefore stated twice:
///
///   * Angles are RADIANS everywhere inside the engine. Degrees appear only in
///     scenario files and in the JSON wire format, and conversion happens solely at
///     the io/ boundary. A `Deg`-suffixed name inside the engine is a bug.
///
///   * Fuel is a fraction of THAT VEHICLE'S OWN delta-v budget. A 13.6% fuel cost for
///     RV-02 and 13.6% for RV-03 are different quantities of delta-v. Fuel fractions
///     are not comparable across vehicles with different budgets.
///     (SIMULATION_MODEL.md §6.)
///
/// Mass is deliberately not modelled, so the rocket equation is never applied and
/// propellant mass in kilograms is never reported.

#include "Constants.hpp"

namespace orca {

/// Semantic aliases. These are documentation, NOT type safety — they are all `double`
/// and the compiler will not stop you mixing them. They exist so a function signature
/// reads as `periodSec(Kilometres radius)` rather than `double f(double)`.
///
/// Strong unit types were considered and rejected for now: the wrapper cost falls on
/// every arithmetic expression in the orbital maths, which is where readability matters
/// most. Strong typing is applied where it catches a class of bug that review does not —
/// entity IDs (see Types.hpp) — and not where it merely adds ceremony.
using Kilometres = double;
using Seconds = double;
using Radians = double;
using Degrees = double;
using KmPerSec = double;
using Fraction = double;  ///< constrained to [0,1]

/// Degrees to radians. Used only at the io/ boundary.
[[nodiscard]] constexpr Radians degToRad(Degrees deg) noexcept {
    return deg * (kPi / 180.0);
}

/// Radians to degrees. Used only at the io/ boundary.
[[nodiscard]] constexpr Degrees radToDeg(Radians rad) noexcept {
    return rad * (180.0 / kPi);
}

/// Orbital radius from altitude above the mean equatorial radius.
[[nodiscard]] constexpr Kilometres radiusFromAltitudeKm(Kilometres altitudeKm) noexcept {
    return altitudeKm + kEarthRadiusKm;
}

/// Altitude above the mean equatorial radius from orbital radius.
[[nodiscard]] constexpr Kilometres altitudeFromRadiusKm(Kilometres radiusKm) noexcept {
    return radiusKm - kEarthRadiusKm;
}

}  // namespace orca
