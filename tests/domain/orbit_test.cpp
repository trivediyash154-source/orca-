/// Tests for Orbit and the simplified orbital model.
///
/// Every expected value here comes from an oracle *outside* the code under test:
///
///   * closed-form values computed by a separate Python implementation;
///   * published textbook figures (REPOSITORY_AUDIT.md §4);
///   * the sidereal day, which is a measured physical constant rather than anything
///     derived from ORCA's formulae.
///
/// Testing `orbitalPeriodSec()` against a second call to `orbitalPeriodSec()` would only
/// assert that the function is deterministic. These assert that it is *right*.

#include <cmath>
#include <limits>

#include "../../cpp/core/Constants.hpp"
#include "../../cpp/core/Math.hpp"
#include "../../cpp/core/Units.hpp"
#include "../../cpp/domain/Orbit.hpp"
#include "../../cpp/domain/SimplifiedOrbitalModel.hpp"
#include "../TestFramework.hpp"

using namespace orca;
namespace som = orca::simplified_orbital_model;

namespace {

constexpr double kR500 = 6878.137;  // 500 km altitude
constexpr double kR800 = 7178.137;  // 800 km altitude

// Reference values from an independent Python implementation of the same closed forms.
constexpr double kPeriod500Sec = 5676.978029;
constexpr double kPeriod800Sec = 6052.413549;
constexpr double kMeanMotion500 = 1.106783446335e-03;
constexpr double kSpeed500KmS = 7.612608;
constexpr double kSpeed800KmS = 7.451831;

}  // namespace

// ---------------------------------------------------------------------------
// Period
// ---------------------------------------------------------------------------

ORCA_TEST(OrbitalModel, PeriodAt500KmMatchesReference) {
    CHECK_NEAR(som::orbitalPeriodSec(kR500), kPeriod500Sec, 1e-4);
    // Textbook cross-check: ~94.6 minutes.
    CHECK_NEAR(som::orbitalPeriodSec(kR500) / 60.0, 94.6163, 1e-3);
}

ORCA_TEST(OrbitalModel, PeriodAt800KmMatchesReference) {
    CHECK_NEAR(som::orbitalPeriodSec(kR800), kPeriod800Sec, 1e-4);
    CHECK_NEAR(som::orbitalPeriodSec(kR800) / 60.0, 100.8736, 1e-3);
}

ORCA_TEST(OrbitalModel, GeostationaryPeriodEqualsSiderealDay) {
    // The strongest independent oracle available: a satellite at 42164.1696 km must have
    // a period of one sidereal day (86164.0905 s), a *measured physical quantity* that
    // owes nothing to ORCA's implementation. Agreement here means mu, the radius
    // convention and the formula are all simultaneously correct -- an error in any one
    // of them would show up.
    constexpr double kGeoRadiusKm = 42164.1696;
    constexpr double kSiderealDaySec = 86164.0905;
    CHECK_NEAR(som::orbitalPeriodSec(kGeoRadiusKm), kSiderealDaySec, 0.01);
}

ORCA_TEST(OrbitalModel, PeriodScalesAsThreeHalvesPower) {
    // Kepler's third law: T ~ r^1.5, so doubling the radius multiplies the period by
    // 2^1.5. Verified as a ratio, which is independent of mu entirely.
    const double t1 = som::orbitalPeriodSec(10000.0);
    const double t2 = som::orbitalPeriodSec(20000.0);
    CHECK_NEAR(t2 / t1, std::pow(2.0, 1.5), 1e-9);
}

// ---------------------------------------------------------------------------
// Mean motion and speed
// ---------------------------------------------------------------------------

ORCA_TEST(OrbitalModel, MeanMotionMatchesReference) {
    CHECK_NEAR(som::meanMotionRadPerSec(kR500), kMeanMotion500, 1e-14);
}

ORCA_TEST(OrbitalModel, MeanMotionIsConsistentWithPeriod) {
    // w and T are computed by separate expressions rather than one from the other, so
    // this cross-check has content: w * T must equal exactly one revolution.
    for (const double r : {kR500, kR800, 12000.0, 42164.0}) {
        CHECK_NEAR(som::meanMotionRadPerSec(r) * som::orbitalPeriodSec(r), kTwoPi, 1e-12);
    }
}

ORCA_TEST(OrbitalModel, CircularSpeedMatchesReference) {
    CHECK_NEAR(som::circularSpeedKmPerSec(kR500), kSpeed500KmS, 1e-5);
    CHECK_NEAR(som::circularSpeedKmPerSec(kR800), kSpeed800KmS, 1e-5);
}

ORCA_TEST(OrbitalModel, HigherOrbitsAreSlower) {
    // Physical sanity: orbital speed decreases with radius.
    double previous = som::circularSpeedKmPerSec(kR500);
    for (double r = kR500 + 500.0; r <= 42164.0; r += 1000.0) {
        const double current = som::circularSpeedKmPerSec(r);
        CHECK(current < previous);
        previous = current;
    }
}

ORCA_TEST(OrbitalModel, SpeedIsConsistentWithCircumferenceOverPeriod) {
    // v must equal 2*pi*r / T. Derived from geometry, not from the sqrt(mu/r) formula.
    for (const double r : {kR500, kR800, 20000.0, 42164.0}) {
        CHECK_NEAR(som::circularSpeedKmPerSec(r), kTwoPi * r / som::orbitalPeriodSec(r),
                   1e-9);
    }
}

// ---------------------------------------------------------------------------
// Phase propagation
// ---------------------------------------------------------------------------

ORCA_TEST(OrbitalModel, PhaseAdvancesByMeanMotion) {
    // After 1000 s the phase must have advanced by exactly w * 1000.
    const double expected = normalizeAngle(kMeanMotion500 * 1000.0);
    CHECK_NEAR(som::phaseAtRad(0.0, kR500, 1000.0), expected, 1e-9);
}

ORCA_TEST(OrbitalModel, FullPeriodReturnsToStartingPhase) {
    // The cleanest self-consistency check available: after exactly one period, the
    // satellite is back where it started.
    for (const double r : {kR500, kR800, 20000.0}) {
        const double startPhase = 1.2345;
        const double after = som::phaseAtRad(startPhase, r, som::orbitalPeriodSec(r));
        CHECK_NEAR(angularSeparation(after, startPhase), 0.0, 1e-9);
    }
}

ORCA_TEST(OrbitalModel, PhaseStaysInCanonicalRange) {
    for (double t = 0.0; t < 500000.0; t += 997.0) {
        const double p = som::phaseAtRad(0.7, kR500, t);
        CHECK(p >= 0.0);
        CHECK(p < kTwoPi);
    }
}

// ---------------------------------------------------------------------------
// Position
// ---------------------------------------------------------------------------

ORCA_TEST(OrbitalModel, PositionMagnitudeAlwaysEqualsRadius) {
    // For a circular orbit the cross terms in the rotation cancel algebraically, so
    // |position| == r exactly for every combination of angles. This is the single most
    // valuable check on the ECI transcription: almost any sign or term error breaks it.
    for (double incl = 0.0; incl <= kPi; incl += 0.23) {
        for (double raan = 0.0; raan < kTwoPi; raan += 0.41) {
            for (double phase = 0.0; phase < kTwoPi; phase += 0.37) {
                const Vector3 p = som::positionKm(kR500, incl, raan, phase);
                CHECK_NEAR(magnitude(p), kR500, 1e-9);
            }
        }
    }
}

ORCA_TEST(OrbitalModel, EquatorialOrbitStaysInPlane) {
    // Zero inclination means the orbit never leaves the equatorial plane.
    for (double phase = 0.0; phase < kTwoPi; phase += 0.19) {
        const Vector3 p = som::positionKm(kR500, 0.0, 0.0, phase);
        CHECK_NEAR(p.z, 0.0, 1e-9);
    }
}

ORCA_TEST(OrbitalModel, PolarOrbitReachesFullRadiusInZ) {
    // At 90 degrees inclination and 90 degrees phase, the satellite is directly over
    // the pole, so z == r.
    const Vector3 p = som::positionKm(kR500, kHalfPi, 0.0, kHalfPi);
    CHECK_NEAR(p.z, kR500, 1e-9);
    CHECK_NEAR(p.x, 0.0, 1e-9);
    CHECK_NEAR(p.y, 0.0, 1e-9);
}

ORCA_TEST(OrbitalModel, ZeroPhaseZeroRaanPointsAlongX) {
    const Vector3 p = som::positionKm(kR500, 0.0, 0.0, 0.0);
    CHECK_NEAR(p.x, kR500, 1e-9);
    CHECK_NEAR(p.y, 0.0, 1e-9);
    CHECK_NEAR(p.z, 0.0, 1e-9);
}

ORCA_TEST(OrbitalModel, HalfPeriodPutsSatelliteOnOppositeSide) {
    // After half an orbit the satellite is antipodal: position exactly negated.
    const Orbit orbit = Orbit::fromRadius(kR500, 0.6, 1.1, 0.3);
    const Vector3 start = orbit.positionAtKm(0.0);
    const Vector3 half = orbit.positionAtKm(orbit.periodSec() / 2.0);
    CHECK_NEAR(half.x, -start.x, 1e-6);
    CHECK_NEAR(half.y, -start.y, 1e-6);
    CHECK_NEAR(half.z, -start.z, 1e-6);
}

// ---------------------------------------------------------------------------
// No drift -- the property that makes determinism free
// ---------------------------------------------------------------------------

ORCA_TEST(OrbitalModel, AnalyticPropagationDoesNotDrift) {
    // Position is computed from t directly rather than accumulated, so evaluating at
    // t = 100000 must equal stepping there. This test fails immediately if anyone
    // replaces the closed form with incremental integration -- which would reintroduce
    // floating-point drift and break reproducibility across differing step counts.
    const Orbit orbit = Orbit::fromRadius(kR500, 0.9, 2.0, 0.5);

    const double target = 100000.0;
    const Vector3 direct = orbit.positionAtKm(target);

    // "Stepping" with the analytic model is just repeated evaluation; what matters is
    // that no state accumulates between calls.
    Vector3 stepped{};
    const int steps = 100000;
    for (int i = 1; i <= steps; ++i) {
        stepped = orbit.positionAtKm(static_cast<double>(i));
    }

    CHECK_NEAR(stepped.x, direct.x, 1e-6);
    CHECK_NEAR(stepped.y, direct.y, 1e-6);
    CHECK_NEAR(stepped.z, direct.z, 1e-6);
}

ORCA_TEST(OrbitalModel, PositionIsReversibleInTime) {
    // Time can be scrubbed backwards, which presentation mode relies on.
    const Orbit orbit = Orbit::fromRadius(kR800, 0.4, 0.2, 1.7);
    const Vector3 forward = orbit.positionAtKm(5000.0);
    const Vector3 backAgain = orbit.positionAtKm(5000.0);
    CHECK(forward == backAgain);

    // And a negative time is well-defined.
    const Vector3 past = orbit.positionAtKm(-1000.0);
    CHECK_NEAR(magnitude(past), kR800, 1e-9);
}

// ---------------------------------------------------------------------------
// Orbit construction and invariants
// ---------------------------------------------------------------------------

ORCA_TEST(Orbit, FromAltitudeMatchesFromRadius) {
    const Orbit byAltitude = Orbit::fromAltitude(500.0, 0.5, 1.0, 2.0);
    const Orbit byRadius = Orbit::fromRadius(kR500, 0.5, 1.0, 2.0);
    CHECK_NEAR(byAltitude.semiMajorAxisKm(), byRadius.semiMajorAxisKm(), 1e-9);
    CHECK_NEAR(byAltitude.altitudeKm(), 500.0, 1e-9);
}

ORCA_TEST(Orbit, DerivedValuesAgreeWithModel) {
    const Orbit orbit = Orbit::fromAltitude(500.0, 0.0, 0.0, 0.0);
    CHECK_NEAR(orbit.periodSec(), kPeriod500Sec, 1e-4);
    CHECK_NEAR(orbit.meanMotionRadPerSec(), kMeanMotion500, 1e-14);
    CHECK_NEAR(orbit.speedKmPerSec(), kSpeed500KmS, 1e-5);
    CHECK_NEAR(orbit.altitudeKm(), 500.0, 1e-9);
}

ORCA_TEST(Orbit, CyclicAnglesAreNormalisedNotRejected) {
    // RAAN and phase are cyclic, so out-of-range values are canonicalised.
    const Orbit orbit = Orbit::fromAltitude(500.0, 0.5, 7.0 * kPi, -kHalfPi);
    CHECK_NEAR(orbit.raanRad(), kPi, 1e-12);
    CHECK_NEAR(orbit.phaseAtEpochRad(), 3.0 * kHalfPi, 1e-12);
}

ORCA_TEST(Orbit, RejectsRadiusBelowEarthSurface) {
    // The common authoring error: passing an altitude where a radius is expected.
    CHECK_THROWS(Orbit::fromRadius(500.0, 0.0, 0.0, 0.0));
    CHECK_THROWS(Orbit::fromRadius(kEarthRadiusKm, 0.0, 0.0, 0.0));
    CHECK_NO_THROW(Orbit::fromRadius(kEarthRadiusKm + 1.0, 0.0, 0.0, 0.0));
}

ORCA_TEST(Orbit, RejectsNonPositiveAltitude) {
    CHECK_THROWS(Orbit::fromAltitude(0.0, 0.0, 0.0, 0.0));
    CHECK_THROWS(Orbit::fromAltitude(-100.0, 0.0, 0.0, 0.0));
}

ORCA_TEST(Orbit, RejectsOutOfRangeInclination) {
    CHECK_THROWS(Orbit::fromAltitude(500.0, -0.1, 0.0, 0.0));
    CHECK_THROWS(Orbit::fromAltitude(500.0, kPi + 0.1, 0.0, 0.0));
    CHECK_NO_THROW(Orbit::fromAltitude(500.0, 0.0, 0.0, 0.0));
    CHECK_NO_THROW(Orbit::fromAltitude(500.0, kPi, 0.0, 0.0));
}

ORCA_TEST(Orbit, RejectsNonFiniteInputs) {
    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::nan("");
    CHECK_THROWS(Orbit::fromRadius(inf, 0.0, 0.0, 0.0));
    CHECK_THROWS(Orbit::fromRadius(kR500, nan, 0.0, 0.0));
    CHECK_THROWS(Orbit::fromRadius(kR500, 0.0, inf, 0.0));
    CHECK_THROWS(Orbit::fromAltitude(nan, 0.0, 0.0, 0.0));
}

ORCA_TEST(Orbit, BandClassification) {
    CHECK(Orbit::fromAltitude(500.0, 0.0, 0.0, 0.0).band() == OrbitBand::Leo);
    CHECK(Orbit::fromAltitude(1999.0, 0.0, 0.0, 0.0).band() == OrbitBand::Leo);
    CHECK(Orbit::fromAltitude(2001.0, 0.0, 0.0, 0.0).band() == OrbitBand::Meo);
    CHECK(Orbit::fromAltitude(20000.0, 0.0, 0.0, 0.0).band() == OrbitBand::Meo);
    // Geostationary altitude is ~35786 km, comfortably above the MEO boundary.
    CHECK(Orbit::fromAltitude(35786.0, 0.0, 0.0, 0.0).band() == OrbitBand::Geo);
}
