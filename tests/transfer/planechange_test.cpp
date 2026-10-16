/// Tests for the plane-change delta-v maths.
///
/// Expected values come from oracles outside the implementation: a separate Python
/// implementation of the closed forms, and trigonometric identities the formulae must
/// satisfy regardless of their coefficients.

#include <cmath>

#include "../../cpp/core/Constants.hpp"
#include "../../cpp/core/Units.hpp"
#include "../../cpp/transfer/PlaneChange.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {

constexpr KmPerSec kSpeed500 = 7.612608173;   // circular speed at 500 km altitude
constexpr KmPerSec kPlaneChange1DegAt500 = 0.132863391;

}  // namespace

// ---------------------------------------------------------------------------
// Plane change
// ---------------------------------------------------------------------------

ORCA_TEST(PlaneChange, ZeroInclinationCostsNothing) {
    CHECK_NEAR(plane_change::pureDeltaVKms(kSpeed500, 0.0), 0.0, 1e-15);
}

ORCA_TEST(PlaneChange, MatchesReferenceValues) {
    // 2*v*sin(di/2), evaluated independently.
    CHECK_NEAR(plane_change::pureDeltaVKms(kSpeed500, degToRad(1.0)),
               kPlaneChange1DegAt500, 1e-8);
    CHECK_NEAR(plane_change::pureDeltaVKms(kSpeed500, degToRad(10.0)), 1.326965039, 1e-8);
    CHECK_NEAR(plane_change::pureDeltaVKms(kSpeed500, degToRad(90.0)), 10.765853724, 1e-8);
    CHECK_NEAR(plane_change::pureDeltaVKms(kSpeed500, degToRad(180.0)), 15.225216346, 1e-8);
}

ORCA_TEST(PlaneChange, OneEightyDegreesCostsTwiceOrbitalSpeed) {
    // Reversing the orbit means reversing the velocity vector: dv = 2v exactly. An
    // identity, independent of any reference table.
    CHECK_NEAR(plane_change::pureDeltaVKms(kSpeed500, kPi), 2.0 * kSpeed500, 1e-12);
}

ORCA_TEST(PlaneChange, IsSymmetricInSign) {
    for (double deg = 0.0; deg <= 180.0; deg += 7.5) {
        const Radians di = degToRad(deg);
        CHECK_NEAR(plane_change::pureDeltaVKms(kSpeed500, di),
                   plane_change::pureDeltaVKms(kSpeed500, -di), 1e-15);
    }
}

ORCA_TEST(PlaneChange, IsMonotonicInInclination) {
    double previous = -1.0;
    for (double deg = 0.0; deg <= 180.0; deg += 2.5) {
        const double dv = plane_change::pureDeltaVKms(kSpeed500, degToRad(deg));
        CHECK(dv >= previous);
        previous = dv;
    }
}

ORCA_TEST(PlaneChange, ScalesLinearlyWithSpeed) {
    // dv is proportional to v, so doubling the speed doubles the cost. Identity check.
    const Radians di = degToRad(23.0);
    CHECK_NEAR(plane_change::pureDeltaVKms(2.0 * kSpeed500, di),
               2.0 * plane_change::pureDeltaVKms(kSpeed500, di), 1e-12);
}

ORCA_TEST(PlaneChange, CombinedDegeneratesToPureWhenSpeedsAreEqual) {
    // sqrt(v^2 + v^2 - 2v^2 cos di) == 2v sin(di/2) is a trigonometric identity. If the
    // combined formula is transcribed wrongly, this breaks.
    for (double deg = 0.0; deg <= 180.0; deg += 5.0) {
        const Radians di = degToRad(deg);
        CHECK_NEAR(plane_change::combinedDeltaVKms(kSpeed500, kSpeed500, di),
                   plane_change::pureDeltaVKms(kSpeed500, di), 1e-10);
    }
}

ORCA_TEST(PlaneChange, CombinedDegeneratesToSpeedDifferenceWhenCoplanar) {
    // At di == 0 the velocity triangle collapses to a line: dv == |v2 - v1|.
    CHECK_NEAR(plane_change::combinedDeltaVKms(7.0, 7.5, 0.0), 0.5, 1e-12);
    CHECK_NEAR(plane_change::combinedDeltaVKms(7.5, 7.0, 0.0), 0.5, 1e-12);
}

ORCA_TEST(PlaneChange, CombinedIsNeverWorseThanSeparateBurns) {
    // Vector addition beats scalar addition: the triangle inequality.
    for (double deg = 0.0; deg <= 180.0; deg += 5.0) {
        const Radians di = degToRad(deg);
        const double combined = plane_change::combinedDeltaVKms(7.5, 7.0, di);
        const double separate = std::fabs(7.5 - 7.0) + plane_change::pureDeltaVKms(7.0, di);
        CHECK(combined <= separate + 1e-12);
    }
}

ORCA_TEST(PlaneChange, CombinedIsNeverNegativeOrNaN) {
    // Catastrophic cancellation guard: v1 ~= v2 with di ~= 0 is the case ORCA evaluates
    // constantly, and an unguarded sqrt of a tiny negative would poison every objective.
    for (double eps = 0.0; eps < 1e-6; eps += 1e-8) {
        const double dv = plane_change::combinedDeltaVKms(7.5, 7.5 + eps, eps);
        CHECK(dv >= 0.0);
        CHECK(std::isfinite(dv));
    }
}

ORCA_TEST(PlaneChange, InclinationDifferenceDoesNotWrap) {
    // 10 deg and 170 deg are near-opposite planes, 160 deg apart -- NOT 20 deg. Angular
    // wrapping would be actively wrong here and would make retrograde transfers look
    // cheap.
    CHECK_NEAR(plane_change::inclinationDifferenceRad(degToRad(10.0), degToRad(170.0)),
               degToRad(160.0), 1e-12);
    CHECK_NEAR(plane_change::inclinationDifferenceRad(degToRad(0.0), degToRad(180.0)),
               kPi, 1e-12);
    CHECK_NEAR(plane_change::inclinationDifferenceRad(degToRad(51.6), degToRad(51.6)),
               0.0, 1e-15);
}

