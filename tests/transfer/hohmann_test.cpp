/// Tests for the Hohmann transfer, the combined manoeuvre, and transfer arc length.
/// Plane-change maths in isolation: planechange_test.cpp
///
/// Expected values come from oracles *outside* the implementation:
///
///   * a separate Python implementation of the same closed forms;
///   * published textbook figures (REPOSITORY_AUDIT.md §4);
///   * algebraic identities the formulae must satisfy regardless of their coefficients
///     (combined <= sequential; the same-radius degeneracy; the circular degeneracy of
///     the arc-length approximation).
///
/// The third category is the strongest: it constrains the *shape* of the model rather
/// than a sampled value, so it catches errors that a lucky reference number would not.

#include <cmath>

#include "../../cpp/core/Constants.hpp"
#include "../../cpp/core/Units.hpp"
#include "../../cpp/transfer/HohmannTransfer.hpp"
#include "../../cpp/transfer/PlaneChange.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {

constexpr Kilometres kR500 = 6878.137;   // 500 km altitude
constexpr Kilometres kR800 = 7178.137;   // 800 km altitude
constexpr Kilometres kR300 = 6678.137;   // 300 km altitude
constexpr Kilometres kRGeo = 42164.0;

// Reference values from an independent Python implementation.
constexpr KmPerSec kHohmann500to800Dv = 0.160758528;
constexpr Seconds kHohmann500to800Time = 2931.847068;
constexpr KmPerSec kHohmann300toGeoDv = 3.892554387;
constexpr Seconds kHohmann300toGeoTime = 18990.131738;
constexpr KmPerSec kSpeed500 = 7.612608173;

}  // namespace

// ---------------------------------------------------------------------------
// Hohmann transfer
// ---------------------------------------------------------------------------

ORCA_TEST(Hohmann, MatchesReferenceForAltitudeRaise) {
    const auto e = hohmann::compute(kR500, kR800, 0.0);
    CHECK_NEAR(e.totalDeltaVKms, kHohmann500to800Dv, 1e-8);
    CHECK_NEAR(e.coastTimeSec, kHohmann500to800Time, 1e-4);
    // Individual burns, also independently computed.
    CHECK_NEAR(e.departureDeltaVKms, 0.080808229, 1e-8);
    CHECK_NEAR(e.arrivalDeltaVKms, 0.079950299, 1e-8);
}

ORCA_TEST(Hohmann, MatchesTextbookLeoToGeo) {
    // Published reference: ~3.9 km/s and ~5.25 h.
    const auto e = hohmann::compute(kR300, kRGeo, 0.0);
    CHECK_NEAR(e.totalDeltaVKms, kHohmann300toGeoDv, 1e-8);
    CHECK_NEAR(e.coastTimeSec, kHohmann300toGeoTime, 1e-3);
    CHECK_NEAR(e.coastTimeSec / 3600.0, 5.275, 0.01);
}

ORCA_TEST(Hohmann, DescentCostsTheSameAsAscent) {
    // Coplanar Hohmann is symmetric in delta-v magnitude and time; only the burn order
    // swaps. An identity, not a sampled value.
    const auto up = hohmann::compute(kR300, kRGeo, 0.0);
    const auto down = hohmann::compute(kRGeo, kR300, 0.0);
    CHECK_NEAR(up.totalDeltaVKms, down.totalDeltaVKms, 1e-12);
    CHECK_NEAR(up.coastTimeSec, down.coastTimeSec, 1e-9);
    // The burns exchange roles.
    CHECK_NEAR(up.departureDeltaVKms, down.arrivalDeltaVKms, 1e-12);
    CHECK_NEAR(up.arrivalDeltaVKms, down.departureDeltaVKms, 1e-12);
}

ORCA_TEST(Hohmann, EqualOrbitsCostNothingCoplanar) {
    const auto e = hohmann::compute(kR500, kR500, 0.0);
    CHECK_NEAR(e.totalDeltaVKms, 0.0, 1e-12);
    CHECK_NEAR(e.departureDeltaVKms, 0.0, 1e-12);
    CHECK_NEAR(e.arrivalDeltaVKms, 0.0, 1e-12);
    CHECK_NEAR(e.planeChangeDeltaVKms, 0.0, 1e-12);
}

ORCA_TEST(Hohmann, EqualOrbitsDegenerateToPurePlaneChange) {
    // With r1 == r2 the ellipse becomes the circle, the departure burn vanishes, and the
    // arrival burn must reduce *exactly* to 2*v*sin(di/2). This is the single best check
    // that the combined-manoeuvre formula is wired to the right speeds.
    for (double deg = 0.0; deg <= 90.0; deg += 5.0) {
        const Radians di = degToRad(deg);
        const auto e = hohmann::compute(kR500, kR500, di);
        CHECK_NEAR(e.totalDeltaVKms, plane_change::pureDeltaVKms(kSpeed500, di), 1e-9);
        CHECK_NEAR(e.departureDeltaVKms, 0.0, 1e-12);
    }
}

ORCA_TEST(Hohmann, CoastTimeIsHalfTheEllipsePeriod) {
    // t = pi*sqrt(a^3/mu). Verified against the period formula rather than a table.
    const auto e = hohmann::compute(kR500, kR800, 0.0);
    const double a = e.semiMajorAxisKm;
    const double expected = kPi * std::sqrt(a * a * a / kMuEarth);
    CHECK_NEAR(e.coastTimeSec, expected, 1e-9);
}

ORCA_TEST(Hohmann, EqualOrbitCoastIsHalfTheOrbitalPeriod) {
    // For r1 == r2 the transfer ellipse is the orbit itself, so the coast is half a
    // revolution.
    const auto e = hohmann::compute(kR500, kR500, 0.0);
    const double orbitalPeriod = kTwoPi * std::sqrt(kR500 * kR500 * kR500 / kMuEarth);
    CHECK_NEAR(e.coastTimeSec, orbitalPeriod / 2.0, 1e-9);
}

ORCA_TEST(Hohmann, TransferSpeedsBracketTheCircularSpeeds) {
    // On the transfer ellipse the spacecraft is faster than circular at periapsis and
    // slower than circular at apoapsis. A structural property of the ellipse.
    const auto e = hohmann::compute(kR500, kR800, 0.0);
    CHECK(e.speedOnTransferAtSourceKms > e.speedCircularAtSourceKms);  // periapsis
    CHECK(e.speedOnTransferAtTargetKms < e.speedCircularAtTargetKms);  // apoapsis
}

ORCA_TEST(Hohmann, CombinedIsNeverWorseThanSequential) {
    // The justification for choosing combined burns. Checked across altitudes and
    // inclinations, both raising and lowering.
    const Kilometres radii[] = {kR300, kR500, kR800, 12000.0, kRGeo};
    for (const Kilometres r1 : radii) {
        for (const Kilometres r2 : radii) {
            for (double deg = 0.0; deg <= 60.0; deg += 5.0) {
                const Radians di = degToRad(deg);
                const double combined = hohmann::compute(r1, r2, di).totalDeltaVKms;
                const double sequential = hohmann::sequentialDeltaVKms(r1, r2, di);
                CHECK(combined <= sequential + 1e-9);
            }
        }
    }
}

ORCA_TEST(Hohmann, CombinedSavingIsMaterial) {
    // Not merely "never worse" -- worth having. Independently computed: ~9.7% for a
    // 500->800 km transfer with 5 degrees of inclination change.
    const Radians di = degToRad(5.0);
    const double combined = hohmann::compute(kR500, kR800, di).totalDeltaVKms;
    const double sequential = hohmann::sequentialDeltaVKms(kR500, kR800, di);
    CHECK_NEAR(combined, 0.732324205, 1e-8);
    CHECK_NEAR(sequential, 0.810847163, 1e-8);
    CHECK((1.0 - combined / sequential) > 0.09);
}

ORCA_TEST(Hohmann, PlaneChangeShareIsZeroWhenCoplanar) {
    const auto e = hohmann::compute(kR500, kR800, 0.0);
    CHECK_NEAR(e.planeChangeDeltaVKms, 0.0, 1e-12);
}

ORCA_TEST(Hohmann, PlaneChangeShareIsPositiveAndBoundedWhenInclined) {
    const auto e = hohmann::compute(kR500, kR800, degToRad(20.0));
    CHECK(e.planeChangeDeltaVKms > 0.0);
    CHECK(e.planeChangeDeltaVKms <= e.totalDeltaVKms);
}

ORCA_TEST(Hohmann, DeltaVIsMonotonicInInclination) {
    double previous = -1.0;
    for (double deg = 0.0; deg <= 90.0; deg += 2.5) {
        const double dv = hohmann::compute(kR500, kR800, degToRad(deg)).totalDeltaVKms;
        CHECK(dv >= previous);
        previous = dv;
    }
}

// ---------------------------------------------------------------------------
// The Phase 0 headline finding, re-derived
// ---------------------------------------------------------------------------

ORCA_TEST(Hohmann, InclinationDominatesAltitude) {
    // REPOSITORY_AUDIT.md §4.1's corrected claim, verified against this implementation
    // rather than copied from the document.
    const double planeChange1Deg = plane_change::pureDeltaVKms(kSpeed500, degToRad(1.0));
    const double altitudeRaise300Km = hohmann::compute(kR500, kR800, 0.0).totalDeltaVKms;

    CHECK_NEAR(planeChange1Deg, 0.132863391, 1e-8);
    CHECK_NEAR(altitudeRaise300Km, 0.160758528, 1e-8);

    // One degree of inclination costs 83% of a 300 km altitude raise.
    CHECK_NEAR(planeChange1Deg / altitudeRaise300Km, 0.826478, 1e-5);

    // And it is *less*, not more -- the arithmetic the inherited Phase 0 audit got
    // backwards. This test exists specifically to stop that error returning.
    CHECK(planeChange1Deg < altitudeRaise300Km);
}

ORCA_TEST(Hohmann, BreakEvenInclinationIsAboutOnePointTwoDegrees) {
    // Solve 2*v*sin(di/2) == dv_hohmann for di, using the implementation's own numbers,
    // then check against the independently derived 1.209961 degrees.
    const double altitudeRaise = hohmann::compute(kR500, kR800, 0.0).totalDeltaVKms;
    const double breakEvenRad = 2.0 * std::asin(altitudeRaise / (2.0 * kSpeed500));
    CHECK_NEAR(radToDeg(breakEvenRad), 1.209961, 1e-5);

    // Confirm it really is the crossover: just below, the plane change is cheaper;
    // just above, more expensive.
    CHECK(plane_change::pureDeltaVKms(kSpeed500, breakEvenRad * 0.99) < altitudeRaise);
    CHECK(plane_change::pureDeltaVKms(kSpeed500, breakEvenRad * 1.01) > altitudeRaise);
}

// ---------------------------------------------------------------------------
// Transfer arc length
// ---------------------------------------------------------------------------

ORCA_TEST(ArcLength, CircleDegeneracyIsExact) {
    // Ramanujan II is exact for a circle. For r1 == r2 the half-perimeter must be
    // exactly pi*r -- a closed-form oracle owing nothing to the approximation.
    CHECK_NEAR(hohmann::transferArcLengthKm(kR500, kR500), kPi * kR500, 1e-6);
    CHECK_NEAR(hohmann::transferArcLengthKm(kRGeo, kRGeo), kPi * kRGeo, 1e-5);
}

ORCA_TEST(ArcLength, MatchesReferenceValues) {
    CHECK_NEAR(hohmann::transferArcLengthKm(kR500, kR800), 22077.028965, 1e-3);
    CHECK_NEAR(hohmann::transferArcLengthKm(kR300, kRGeo), 65276.541590, 1e-3);
}

ORCA_TEST(ArcLength, IsSymmetric) {
    // The ellipse does not care which end you start from.
    CHECK_NEAR(hohmann::transferArcLengthKm(kR500, kRGeo),
               hohmann::transferArcLengthKm(kRGeo, kR500), 1e-9);
}

ORCA_TEST(ArcLength, IsBracketedByTheTwoSemicircles) {
    // The transfer arc must be longer than a semicircle at the smaller radius and shorter
    // than one at the larger. A geometric bound, independent of the approximation used.
    const Kilometres radii[] = {kR300, kR500, kR800, 12000.0, kRGeo};
    for (const Kilometres r1 : radii) {
        for (const Kilometres r2 : radii) {
            const double arc = hohmann::transferArcLengthKm(r1, r2);
            const double lower = kPi * std::fmin(r1, r2);
            const double upper = kPi * std::fmax(r1, r2);
            CHECK(arc >= lower - 1e-6);
            CHECK(arc <= upper + 1e-6);
        }
    }
}

ORCA_TEST(ArcLength, IsIndependentOfInclination) {
    // Deliberate: arc length depends only on the two radii. That is precisely why
    // `distance` disagrees with `fuel` as an objective -- inclination is expensive in
    // delta-v and free in distance (OPTIMIZATION_MODEL.md §2.1).
    const auto coplanar = hohmann::compute(kR500, kR800, 0.0);
    const auto inclined = hohmann::compute(kR500, kR800, degToRad(30.0));
    CHECK_NEAR(coplanar.pathLengthKm, inclined.pathLengthKm, 1e-12);
    CHECK(inclined.totalDeltaVKms > coplanar.totalDeltaVKms);  // but delta-v differs
}
