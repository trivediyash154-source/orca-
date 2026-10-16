/// Basic structural properties of `astar_heuristic::estimateRemainingTimeSec` and
/// `computeBreakdown`: zero-distance, symmetry, independence from inclination and phase,
/// finiteness at extreme separation, and the `Breakdown`/`estimateRemainingTimeSec`
/// agreement invariant.
///
/// These are properties of the *function*, checked without needing an external reference
/// value — each one follows directly from the formula's definition
/// (`h(n) = min(coast(r(n),r(goal)), coast(r(n),R_earth)+coast(R_earth,r(goal)))`,
/// AStarHeuristic.hpp) and would be true of any correct implementation of it. Independent
/// verification against real, non-heuristic computation — the actual admissibility and
/// consistency claims — lives in heuristic_admissibility_test.cpp and
/// heuristic_consistency_test.cpp; this file is the cheaper, structural layer underneath
/// those.

#include <cmath>

#include "../../cpp/algorithms/AStarHeuristic.hpp"
#include "../../cpp/core/Constants.hpp"
#include "../TestFramework.hpp"

using namespace orca;
using namespace orca::astar_heuristic;

namespace {

Orbit orbitAt(Kilometres altitudeKm, Radians phaseRad, Radians inclRad = 0.0) {
    return Orbit::fromAltitude(altitudeKm, inclRad, 0.0, phaseRad);
}

}  // namespace

// ---------------------------------------------------------------------------
// Zero distance and symmetry
// ---------------------------------------------------------------------------

ORCA_TEST(AStarHeuristic, SameRadiusIsZeroRegardlessOfPhaseOrInclination) {
    const Orbit a = orbitAt(500.0, 0.0, 0.0);
    const Orbit b = orbitAt(500.0, 3.7, degToRad(45.0));
    CHECK_NEAR(estimateRemainingTimeSec(a, b), 0.0, 1e-9);
    CHECK_NEAR(estimateRemainingTimeSec(b, a), 0.0, 1e-9);
}

ORCA_TEST(AStarHeuristic, SourceEqualsTargetOrbitIsZero) {
    const Orbit a = orbitAt(700.0, 1.234, degToRad(12.0));
    CHECK_NEAR(estimateRemainingTimeSec(a, a), 0.0, 1e-9);
}

ORCA_TEST(AStarHeuristic, IsSymmetric) {
    // coast(ra,rb) depends on (ra+rb)/2, which is symmetric in its two arguments, and the
    // floor-detour term is a sum of two symmetric coast() calls -- so h itself must be
    // symmetric. This is a direct algebraic consequence of the formula, checked here so a
    // future edit that breaks it (e.g. an accidental directional term) is caught.
    const Orbit a = orbitAt(500.0, 0.0);
    const Orbit b = orbitAt(35786.0, 2.0);  // geostationary-altitude scale
    CHECK_NEAR(estimateRemainingTimeSec(a, b), estimateRemainingTimeSec(b, a), 1e-6);
}

// ---------------------------------------------------------------------------
// Independence from quantities the derivation says must not matter
// (AStarHeuristic.hpp §2 and §6: no time argument, no inclination term)
// ---------------------------------------------------------------------------

ORCA_TEST(AStarHeuristic, IgnoresInclinationEntirely) {
    const Orbit goal = orbitAt(1200.0, 0.0);
    const Orbit equatorial = orbitAt(500.0, 0.0, 0.0);
    const Orbit polar = orbitAt(500.0, 0.0, degToRad(90.0));
    const Orbit retrograde = orbitAt(500.0, 0.0, degToRad(179.0));
    const Seconds hEquatorial = estimateRemainingTimeSec(equatorial, goal);
    CHECK_NEAR(estimateRemainingTimeSec(polar, goal), hEquatorial, 1e-9);
    CHECK_NEAR(estimateRemainingTimeSec(retrograde, goal), hEquatorial, 1e-9);
}

ORCA_TEST(AStarHeuristic, IgnoresPhaseIncludingNearWrapAround) {
    const Orbit goal = orbitAt(1200.0, 0.0);
    const Seconds hAtZero = estimateRemainingTimeSec(orbitAt(500.0, 0.0), goal);
    CHECK_NEAR(estimateRemainingTimeSec(orbitAt(500.0, 3.0), goal), hAtZero, 1e-9);
    // Phase values straddling the 2*pi wrap point are normalised internally (Orbit.hpp);
    // the heuristic must be unaffected by that either way, exercised right at the seam.
    CHECK_NEAR(estimateRemainingTimeSec(orbitAt(500.0, 0.0001), goal), hAtZero, 1e-9);
    CHECK_NEAR(estimateRemainingTimeSec(orbitAt(500.0, 6.2830), goal), hAtZero, 1e-9);
}

// ---------------------------------------------------------------------------
// Robustness: finite, non-negative, no NaN -- including well beyond ORCA's realistic
// range, where AStarHeuristic.hpp §3 documents admissibility as no longer proven (only the
// floor-detour term remains unconditionally safe there). This test does not assert
// admissibility at that extreme -- only that the function stays well-defined.
// ---------------------------------------------------------------------------

ORCA_TEST(AStarHeuristic, FiniteAndNonNegativeAcrossRealisticRange) {
    const double altitudesKm[] = {200.0, 500.0, 800.0, 1200.0, 2000.0,
                                  5000.0, 20000.0, 35786.0};
    for (double a : altitudesKm) {
        for (double b : altitudesKm) {
            const Seconds h = estimateRemainingTimeSec(orbitAt(a, 0.0), orbitAt(b, 1.0));
            CHECK(!std::isnan(h));
            CHECK(std::isfinite(h));
            CHECK(h >= 0.0);
        }
    }
}

ORCA_TEST(AStarHeuristic, FiniteAndNonNegativeAtExtremeSeparation) {
    // Far beyond any ORCA scenario (PRD.md scopes the project to Earth-orbital rescue,
    // topping out around GEO ~35,786 km) and beyond the empirically-checked admissibility
    // range (AStarHeuristic.hpp §3: violations only appear past ~1e6-1e7 km). The
    // requirement here is only well-definedness, not admissibility.
    const Orbit near = orbitAt(500.0, 0.0);
    const Orbit farAway = orbitAt(50000000.0, 1.0);  // 5e7 km altitude
    const Seconds h = estimateRemainingTimeSec(near, farAway);
    CHECK(!std::isnan(h));
    CHECK(std::isfinite(h));
    CHECK(h >= 0.0);
}

ORCA_TEST(AStarHeuristic, MinimumAltitudeJustAboveEarthSurfaceIsWellDefined) {
    // Orbit::fromAltitude rejects altitude <= 0 (radius at or below Earth's surface), so
    // the closest legal probe is a thin sliver above it -- exactly the regime the
    // floor-detour term's proof is about (AStarHeuristic.hpp §4).
    const Orbit low = orbitAt(1.0, 0.0);
    const Orbit high = orbitAt(35786.0, 2.0);
    const Seconds h = estimateRemainingTimeSec(low, high);
    CHECK(!std::isnan(h));
    CHECK(std::isfinite(h));
    CHECK(h >= 0.0);
}

// ---------------------------------------------------------------------------
// Breakdown / estimateRemainingTimeSec agreement
// ---------------------------------------------------------------------------

ORCA_TEST(AStarHeuristic, BreakdownChosenMatchesEstimate) {
    const Orbit a = orbitAt(500.0, 0.0);
    const Orbit b = orbitAt(20000.0, 2.5);
    const Breakdown breakdown = computeBreakdown(a, b);
    CHECK_NEAR(breakdown.chosenSec, estimateRemainingTimeSec(a, b), 1e-9);
    CHECK_NEAR(breakdown.chosenSec, std::min(breakdown.directHohmannSec, breakdown.floorDetourSec),
              1e-9);
}

ORCA_TEST(AStarHeuristic, BreakdownFlagMatchesWhichTermWasSmaller) {
    const Orbit a = orbitAt(500.0, 0.0);
    const Orbit b = orbitAt(20000.0, 2.5);
    const Breakdown breakdown = computeBreakdown(a, b);
    if (breakdown.floorDetourWasTighter) {
        CHECK(breakdown.floorDetourSec <= breakdown.directHohmannSec);
    } else {
        CHECK(breakdown.directHohmannSec <= breakdown.floorDetourSec);
    }
}

ORCA_TEST(AStarHeuristic, BreakdownComponentsAreNonNegative) {
    const Orbit a = orbitAt(300.0, 0.0);
    const Orbit b = orbitAt(1000.0, 1.0);
    const Breakdown breakdown = computeBreakdown(a, b);
    CHECK(breakdown.directHohmannSec >= 0.0);
    CHECK(breakdown.floorDetourSec >= 0.0);
}
