#include "Frames.hpp"

#include <cmath>

namespace orca::orbital {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
constexpr double kRadToDeg = 180.0 / kPi;

/// WGS-84 ellipsoid — the frame latitude and longitude are reported in. See the header on why
/// this differs from the WGS-72 constants SGP4 propagates with.
constexpr double kWgs84SemiMajorKm = 6378.137;
constexpr double kWgs84Flattening = 1.0 / 298.257223563;
constexpr double kWgs84EccSq = kWgs84Flattening * (2.0 - kWgs84Flattening);

/// Julian date of the J2000.0 epoch, and days per Julian century.
constexpr double kJ2000 = 2451545.0;
constexpr double kDaysPerJulianCentury = 36525.0;

constexpr double kUnixEpochJulianDate = 2440587.5;
constexpr double kMillisecondsPerDay = 86400000.0;

[[nodiscard]] double wrapTwoPi(double angle) noexcept {
    double wrapped = std::fmod(angle, kTwoPi);
    if (wrapped < 0.0) wrapped += kTwoPi;
    return wrapped;
}

}  // namespace

double julianDateFromUnixMillis(double unixMillis) noexcept {
    return kUnixEpochJulianDate + unixMillis / kMillisecondsPerDay;
}

double greenwichMeanSiderealTime(double julianDate) noexcept {
    const double tut1 = (julianDate - kJ2000) / kDaysPerJulianCentury;

    // IAU-1982 GMST, in seconds of time. The 876600*3600 term is the whole-hours part of the
    // sidereal rate held separately from the fractional remainder, which is what keeps the
    // series accurate over a century instead of losing precision in the leading digits.
    double gmstSeconds = 67310.54841 +
                         (876600.0 * 3600.0 + 8640184.812866) * tut1 +
                         0.093104 * tut1 * tut1 -
                         6.2e-6 * tut1 * tut1 * tut1;

    // One second of time is 1/240 of a degree of rotation.
    const double degreesPerSecondOfTime = 1.0 / 240.0;
    const double gmstRadians = gmstSeconds * degreesPerSecondOfTime * (kPi / 180.0);
    return wrapTwoPi(gmstRadians);
}

Vector3 temeToEcef(const Vector3& temeKm, double gmstRad) noexcept {
    const double cosG = std::cos(gmstRad);
    const double sinG = std::sin(gmstRad);
    // Rotation of -GMST about z: the Earth-fixed frame has turned by +GMST since the inertial
    // x-axis, so expressing an inertial vector in it rotates the other way.
    return Vector3{temeKm.x * cosG + temeKm.y * sinG,
                   -temeKm.x * sinG + temeKm.y * cosG,
                   temeKm.z};
}

Geodetic ecefToGeodetic(const Vector3& ecefKm) noexcept {
    Geodetic out;
    const double equatorialRadius = std::sqrt(ecefKm.x * ecefKm.x + ecefKm.y * ecefKm.y);

    out.longitudeDeg = std::atan2(ecefKm.y, ecefKm.x) * kRadToDeg;

    // At the poles the longitude is undefined and the iteration below divides by cos(lat).
    if (equatorialRadius < 1.0e-9) {
        out.latitudeDeg = (ecefKm.z >= 0.0) ? 90.0 : -90.0;
        out.altitudeKm = std::fabs(ecefKm.z) - kWgs84SemiMajorKm * (1.0 - kWgs84Flattening);
        return out;
    }

    // Geodetic latitude is not the geocentric one: the ellipsoid's surface normal does not pass
    // through the centre. The iteration converges on the latitude whose normal contains this
    // point — the difference peaks near 0.19 degrees at mid latitudes, which is a ~21 km
    // ground error if the geocentric value is used by mistake.
    double latitude = std::atan2(ecefKm.z, equatorialRadius);
    double primeVerticalRadius = kWgs84SemiMajorKm;
    for (int iteration = 0; iteration < 6; ++iteration) {
        const double sinLat = std::sin(latitude);
        primeVerticalRadius = kWgs84SemiMajorKm / std::sqrt(1.0 - kWgs84EccSq * sinLat * sinLat);
        const double altitude = equatorialRadius / std::cos(latitude) - primeVerticalRadius;
        const double correction =
            1.0 - kWgs84EccSq * primeVerticalRadius / (primeVerticalRadius + altitude);
        latitude = std::atan2(ecefKm.z, equatorialRadius * correction);
    }

    const double sinLat = std::sin(latitude);
    primeVerticalRadius = kWgs84SemiMajorKm / std::sqrt(1.0 - kWgs84EccSq * sinLat * sinLat);
    out.latitudeDeg = latitude * kRadToDeg;
    out.altitudeKm = equatorialRadius / std::cos(latitude) - primeVerticalRadius;
    return out;
}

Geodetic temeToGeodetic(const Vector3& temeKm, double julianDate) noexcept {
    return ecefToGeodetic(temeToEcef(temeKm, greenwichMeanSiderealTime(julianDate)));
}

}  // namespace orca::orbital
