/// \file
/// Reference-frame conversions for catalogue propagation.
///
/// ## The frames, named rather than assumed
///
/// Mixing these silently produces a satellite field that looks plausible and puts every object
/// over the wrong part of the world, so each transition is explicit:
///
///   - **TEME** (True Equator, Mean Equinox) — what SGP4 outputs. Commonly called "ECI", and
///     close enough to one for this purpose (sub-kilometre over these epochs), but named
///     honestly here rather than quietly relabelled.
///   - **ECEF** — Earth-fixed, reached by rotating TEME through the Greenwich mean sidereal
///     angle for the instant. This is the only rotation that makes a longitude correct.
///   - **Geodetic** — latitude, longitude, altitude on the WGS-84 ellipsoid.
///
/// ## Two ellipsoids, deliberately
///
/// SGP4 propagates on WGS-72 because that is the model the element sets were fitted against
/// (`Sgp4.cpp`). Geodetic coordinates are reported on WGS-84 because that is what a latitude and
/// longitude mean to every consumer of them — a map, a ground station, a reader. Using WGS-72 for
/// the ellipsoid to "stay consistent" would be consistent with the propagator and wrong about the
/// Earth. The two are separate concerns and the constants below are separate accordingly.

#ifndef ORCA_ORBITAL_FRAMES_HPP
#define ORCA_ORBITAL_FRAMES_HPP

#include "../core/Vector3.hpp"

namespace orca::orbital {

/// Position on the WGS-84 ellipsoid.
struct Geodetic {
    double latitudeDeg = 0.0;   ///< Geodetic latitude, [-90, 90].
    double longitudeDeg = 0.0;  ///< [-180, 180].
    double altitudeKm = 0.0;    ///< Height above the ellipsoid, not above sea level.
};

/// Greenwich Mean Sidereal Time in radians for a Julian date (UT1), by the IAU-1982 series.
/// This single angle is what ties an inertial position to a place on the ground.
[[nodiscard]] double greenwichMeanSiderealTime(double julianDate) noexcept;

/// Julian date from a Unix epoch in milliseconds — the form a browser and a REST payload carry.
[[nodiscard]] double julianDateFromUnixMillis(double unixMillis) noexcept;

/// TEME -> ECEF: a rotation of -GMST about the polar axis. Polar motion is not applied; it is a
/// sub-metre effect, far below the accuracy an SGP4 element set carries in the first place.
[[nodiscard]] Vector3 temeToEcef(const Vector3& temeKm, double gmstRad) noexcept;

/// ECEF -> geodetic on WGS-84, by the standard iteration on latitude. Converges to well under a
/// millimetre in five passes for any orbital altitude.
[[nodiscard]] Geodetic ecefToGeodetic(const Vector3& ecefKm) noexcept;

/// Convenience: TEME straight to geodetic at a Julian date.
[[nodiscard]] Geodetic temeToGeodetic(const Vector3& temeKm, double julianDate) noexcept;

}  // namespace orca::orbital

#endif  // ORCA_ORBITAL_FRAMES_HPP
