/// \file
/// Reference-frame conversions.
///
/// These assert against physics and against known astronomical values, not against recorded
/// output of this code. A frame bug is the kind that produces a picture that looks entirely
/// right — satellites around a globe — while putting every object over the wrong country, so
/// "it renders" is worth nothing here and round-trip identities are worth a great deal.

#include "../TestFramework.hpp"

#include <cmath>

#include "../../cpp/orbital/Frames.hpp"
#include "../../cpp/orbital/Sgp4.hpp"
#include "../../cpp/orbital/Tle.hpp"

namespace {

using orca::orbital::ecefToGeodetic;
using orca::orbital::Geodetic;
using orca::orbital::greenwichMeanSiderealTime;
using orca::orbital::julianDateFromUnixMillis;
using orca::orbital::temeToEcef;
using orca::Vector3;

constexpr double kPi = 3.14159265358979323846;

}  // namespace

ORCA_TEST(Frames, JulianDateMatchesKnownEpochs) {
    // The Unix epoch is JD 2440587.5 by definition, and J2000.0 is JD 2451545.0.
    CHECK_NEAR(julianDateFromUnixMillis(0.0), 2440587.5, 1e-9);
    // 2000-01-01T12:00:00Z in Unix milliseconds.
    CHECK_NEAR(julianDateFromUnixMillis(946728000000.0), 2451545.0, 1e-9);
}

ORCA_TEST(Frames, SiderealTimeAdvancesByOneSiderealDayPerDay) {
    const double jd = 2451545.0;
    const double g0 = greenwichMeanSiderealTime(jd);
    const double g1 = greenwichMeanSiderealTime(jd + 1.0);
    // A solar day is longer than a sidereal day, so after 24 h of civil time the Earth has
    // turned a full revolution PLUS about 3 m 56 s worth of angle -- roughly 0.0172 rad.
    double advance = g1 - g0;
    while (advance < 0.0) advance += 2.0 * kPi;
    CHECK_NEAR(advance, 0.0172021, 1e-4);
}

ORCA_TEST(Frames, SiderealTimeAtJ2000MatchesTheKnownValue) {
    // GMST at J2000.0 is 18h 41m 50.5s = 280.46 degrees, a standard published value.
    const double gmstDeg = greenwichMeanSiderealTime(2451545.0) * 180.0 / kPi;
    CHECK_NEAR(gmstDeg, 280.46, 0.01);
}

ORCA_TEST(Frames, TemeToEcefIsARotationAndPreservesLength) {
    const Vector3 teme{4000.0, -5000.0, 3000.0};
    const Vector3 ecef = temeToEcef(teme, 1.234);
    CHECK_NEAR(magnitude(ecef), magnitude(teme), 1e-9);
    // The polar component is untouched by a rotation about the pole.
    CHECK_NEAR(ecef.z, teme.z, 1e-12);
}

ORCA_TEST(Frames, TemeToEcefIsIdentityAtZeroSiderealAngle) {
    const Vector3 teme{1234.0, -567.0, 890.0};
    const Vector3 ecef = temeToEcef(teme, 0.0);
    CHECK_NEAR(ecef.x, teme.x, 1e-12);
    CHECK_NEAR(ecef.y, teme.y, 1e-12);
    CHECK_NEAR(ecef.z, teme.z, 1e-12);
}

ORCA_TEST(Frames, GeodeticRecoversTheEquatorAndThePoles) {
    // A point on the equator at longitude 0, one Earth radius out.
    const Geodetic equator = ecefToGeodetic(Vector3{6378.137, 0.0, 0.0});
    CHECK_NEAR(equator.latitudeDeg, 0.0, 1e-9);
    CHECK_NEAR(equator.longitudeDeg, 0.0, 1e-9);
    CHECK_NEAR(equator.altitudeKm, 0.0, 1e-6);

    // 90 degrees east.
    const Geodetic east = ecefToGeodetic(Vector3{0.0, 6378.137, 0.0});
    CHECK_NEAR(east.longitudeDeg, 90.0, 1e-9);

    // The pole: the polar radius is a(1-f) = 6356.752 km, so a point there is at zero altitude.
    const Geodetic pole = ecefToGeodetic(Vector3{0.0, 0.0, 6356.752314245});
    CHECK_NEAR(pole.latitudeDeg, 90.0, 1e-6);
    CHECK_NEAR(pole.altitudeKm, 0.0, 1e-5);
}

ORCA_TEST(Frames, GeodeticLatitudeExceedsGeocentricOnAnOblateEarth) {
    // At 45 degrees GEOCENTRIC the geodetic latitude is about 45.19 -- the ellipsoid's surface
    // normal does not pass through the centre. Getting this wrong is a ~21 km ground error, and
    // a spherical conversion would return exactly 45.
    const double r = 6378.137;
    const double c = std::cos(45.0 * kPi / 180.0);
    const double s = std::sin(45.0 * kPi / 180.0);
    const Geodetic g = ecefToGeodetic(Vector3{r * c, 0.0, r * s});
    CHECK(g.latitudeDeg > 45.15);
    CHECK(g.latitudeDeg < 45.25);
}

ORCA_TEST(Frames, AltitudeIsMeasuredFromTheEllipsoidNotTheCentre) {
    // 400 km above the equator.
    const Geodetic g = ecefToGeodetic(Vector3{6378.137 + 400.0, 0.0, 0.0});
    CHECK_NEAR(g.altitudeKm, 400.0, 1e-6);
}

ORCA_TEST(Frames, APropagatedSatelliteLandsOverAPlausibleGroundPoint) {
    // End to end: real element set -> SGP4 -> TEME -> ECEF -> geodetic. The ISS cannot exceed
    // its own inclination by more than the geodetic/geocentric offset, and must be at its real
    // altitude. Both hold only if every step in the chain is right.
    const char* line1 = "1 25544U 98067A   26250.49846501  .00005306  00000+0  10435-3 0  9991";
    const char* line2 = "2 25544  51.6306 252.7093 0004983 115.8922 244.2580 15.49018229584512";
    const auto elements = orca::orbital::parseTle("ISS (ZARYA)", line1, line2);
    CHECK(elements.has_value());
    if (!elements.has_value()) return;

    const auto satellite = orca::orbital::Sgp4Satellite::initialise(*elements);
    CHECK(satellite.has_value());
    if (!satellite.has_value()) return;

    for (double minutes = 0.0; minutes < 95.0; minutes += 5.0) {
        const auto state = satellite->propagate(minutes);
        CHECK(state.has_value());
        if (!state.has_value()) continue;
        const double jd = satellite->epochJulianDate() + minutes / 1440.0;
        const Geodetic g = orca::orbital::temeToGeodetic(state->positionKm, jd);

        CHECK(std::fabs(g.latitudeDeg) <= 51.9);
        CHECK(g.longitudeDeg >= -180.0);
        CHECK(g.longitudeDeg <= 180.0);
        CHECK(g.altitudeKm > 380.0);
        CHECK(g.altitudeKm < 460.0);
    }
}
