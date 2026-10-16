/// Tests for the scalar, unit and angular utilities.
///
/// Angle handling gets the most attention here: orbital phase advances without bound, so
/// every consumer depends on normalisation being correct for arbitrarily large and
/// negative inputs, and on "difference" meaning the *shortest* difference.

#include <cmath>

#include "../../cpp/core/Constants.hpp"
#include "../../cpp/core/Math.hpp"
#include "../../cpp/core/Units.hpp"
#include "../TestFramework.hpp"

using namespace orca;

// ---------------------------------------------------------------------------
// Scalar utilities
// ---------------------------------------------------------------------------

ORCA_TEST(Math, Clamp) {
    CHECK_NEAR(clamp(5.0, 0.0, 10.0), 5.0, 1e-15);
    CHECK_NEAR(clamp(-1.0, 0.0, 10.0), 0.0, 1e-15);
    CHECK_NEAR(clamp(11.0, 0.0, 10.0), 10.0, 1e-15);
    // Boundaries are inclusive.
    CHECK_NEAR(clamp(0.0, 0.0, 10.0), 0.0, 1e-15);
    CHECK_NEAR(clamp(10.0, 0.0, 10.0), 10.0, 1e-15);
}

ORCA_TEST(Math, ClampUnitAndInUnitInterval) {
    CHECK_NEAR(clampUnit(-0.5), 0.0, 1e-15);
    CHECK_NEAR(clampUnit(1.5), 1.0, 1e-15);
    CHECK_NEAR(clampUnit(0.25), 0.25, 1e-15);

    CHECK(inUnitInterval(0.0));
    CHECK(inUnitInterval(1.0));
    CHECK(inUnitInterval(0.5));
    CHECK(!inUnitInterval(-1e-9));
    CHECK(!inUnitInterval(1.0 + 1e-9));
}

ORCA_TEST(Math, NearlyEqual) {
    CHECK(nearlyEqual(1.0, 1.0));
    CHECK(nearlyEqual(1.0, 1.0 + 1e-12));
    CHECK(!nearlyEqual(1.0, 1.1));
    // Tolerance is honoured as given.
    CHECK(nearlyEqual(1.0, 1.05, 0.1));
    CHECK(!nearlyEqual(1.0, 1.05, 0.01));
}

ORCA_TEST(Math, Lerp) {
    CHECK_NEAR(lerp(0.0, 10.0, 0.0), 0.0, 1e-15);
    CHECK_NEAR(lerp(0.0, 10.0, 1.0), 10.0, 1e-15);
    CHECK_NEAR(lerp(0.0, 10.0, 0.25), 2.5, 1e-15);
    CHECK_NEAR(lerp(-5.0, 5.0, 0.5), 0.0, 1e-15);
}

// ---------------------------------------------------------------------------
// Units
// ---------------------------------------------------------------------------

ORCA_TEST(Units, DegreeRadianConversion) {
    CHECK_NEAR(degToRad(180.0), kPi, 1e-15);
    CHECK_NEAR(degToRad(90.0), kHalfPi, 1e-15);
    CHECK_NEAR(degToRad(360.0), kTwoPi, 1e-15);
    CHECK_NEAR(radToDeg(kPi), 180.0, 1e-12);
    // Known non-trivial value: 51.6 deg is the ISS-like inclination used in fixtures.
    // Expected value computed independently (Python math.radians).
    CHECK_NEAR(degToRad(51.6), 0.900589894029, 1e-11);
}

ORCA_TEST(Units, DegreeRadianRoundTrip) {
    // Property: converting there and back is the identity.
    for (double deg = -720.0; deg <= 720.0; deg += 7.5) {
        CHECK_NEAR(radToDeg(degToRad(deg)), deg, 1e-12);
    }
}

ORCA_TEST(Units, AltitudeRadiusRoundTrip) {
    for (double alt = 200.0; alt <= 40000.0; alt += 1234.5) {
        CHECK_NEAR(altitudeFromRadiusKm(radiusFromAltitudeKm(alt)), alt, 1e-9);
    }
    CHECK_NEAR(radiusFromAltitudeKm(500.0), 6878.137, 1e-12);
}

// ---------------------------------------------------------------------------
// Angles
// ---------------------------------------------------------------------------

ORCA_TEST(Angles, NormalizeWrapsLargePositive) {
    // The case named in the Phase 1 brief: 7*pi collapses to pi.
    CHECK_NEAR(normalizeAngle(7.0 * kPi), kPi, 1e-12);
    CHECK_NEAR(normalizeAngle(kTwoPi), 0.0, 1e-12);
    CHECK_NEAR(normalizeAngle(3.0 * kTwoPi), 0.0, 1e-12);
    CHECK_NEAR(normalizeAngle(kTwoPi + kHalfPi), kHalfPi, 1e-12);
}

ORCA_TEST(Angles, NormalizeWrapsNegative) {
    // std::fmod alone is insufficient here: it preserves the sign of the dividend.
    CHECK_NEAR(normalizeAngle(-kHalfPi), 3.0 * kHalfPi, 1e-12);
    CHECK_NEAR(normalizeAngle(-kPi), kPi, 1e-12);
    CHECK_NEAR(normalizeAngle(-kTwoPi), 0.0, 1e-12);
    CHECK_NEAR(normalizeAngle(-7.0 * kPi), kPi, 1e-12);
}

ORCA_TEST(Angles, NormalizeAlwaysInDocumentedInterval) {
    // Property: the result is always in [0, 2*pi), for any input, however extreme.
    const double inputs[] = {0.0,    1.0,     -1.0,   1e-15, -1e-15, kTwoPi, -kTwoPi,
                             1000.0, -1000.0, 1e6,    -1e6,  1e9,    -1e9};
    for (const double a : inputs) {
        const double n = normalizeAngle(a);
        CHECK(n >= 0.0);
        CHECK(n < kTwoPi);
    }
}

ORCA_TEST(Angles, NormalizeIsIdempotent) {
    for (double a = -20.0; a <= 20.0; a += 0.37) {
        CHECK_NEAR(normalizeAngle(normalizeAngle(a)), normalizeAngle(a), 1e-15);
    }
}

ORCA_TEST(Angles, DifferenceTakesShortestPath) {
    // The point of the function: 350 deg to 10 deg is +20 deg, not -340 deg.
    CHECK_NEAR(angularDifference(degToRad(350.0), degToRad(10.0)), degToRad(20.0), 1e-12);
    // And the reverse is -20 deg.
    CHECK_NEAR(angularDifference(degToRad(10.0), degToRad(350.0)), degToRad(-20.0), 1e-12);
    CHECK_NEAR(angularDifference(0.0, kHalfPi), kHalfPi, 1e-12);
    CHECK_NEAR(angularDifference(kHalfPi, 0.0), -kHalfPi, 1e-12);
}

ORCA_TEST(Angles, DifferenceAlwaysInHalfOpenPiInterval) {
    // Property: result lies in (-pi, pi] for every pair.
    for (double a = -10.0; a <= 10.0; a += 0.41) {
        for (double b = -10.0; b <= 10.0; b += 0.53) {
            const double d = angularDifference(a, b);
            CHECK(d > -kPi - 1e-12);
            CHECK(d <= kPi + 1e-12);
        }
    }
}

ORCA_TEST(Angles, DifferenceIsAntisymmetric) {
    // Property: d(a,b) == -d(b,a), except at exactly pi where both map to +pi.
    for (double a = -6.0; a <= 6.0; a += 0.31) {
        for (double b = -6.0; b <= 6.0; b += 0.47) {
            const double forward = angularDifference(a, b);
            const double backward = angularDifference(b, a);
            if (std::fabs(std::fabs(forward) - kPi) < 1e-9) {
                continue;  // the antipodal case has no signed direction
            }
            CHECK_NEAR(forward, -backward, 1e-12);
        }
    }
}

ORCA_TEST(Angles, SeparationIsUnsignedAndBounded) {
    CHECK_NEAR(angularSeparation(degToRad(350.0), degToRad(10.0)), degToRad(20.0), 1e-12);
    CHECK_NEAR(angularSeparation(degToRad(10.0), degToRad(350.0)), degToRad(20.0), 1e-12);
    for (double a = -8.0; a <= 8.0; a += 0.29) {
        for (double b = -8.0; b <= 8.0; b += 0.43) {
            const double s = angularSeparation(a, b);
            CHECK(s >= 0.0);
            CHECK(s <= kPi + 1e-12);
        }
    }
}
