/// Tests for the rendezvous phasing model.

#include <cmath>
#include <limits>

#include "../../cpp/core/Constants.hpp"
#include "../../cpp/core/Math.hpp"
#include "../../cpp/core/Units.hpp"
#include "../../cpp/transfer/HohmannTransfer.hpp"
#include "../../cpp/transfer/PhasingModel.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {

Orbit orbitAt(Kilometres altitudeKm, Radians phaseRad, Radians inclinationRad = 0.0) {
    return Orbit::fromAltitude(altitudeKm, inclinationRad, 0.0, phaseRad);
}

/// Coast time and arc for a Hohmann between two altitudes.
Seconds coastFor(Kilometres altA, Kilometres altB) {
    return hohmann::compute(radiusFromAltitudeKm(altA), radiusFromAltitudeKm(altB), 0.0)
        .coastTimeSec;
}

}  // namespace

// ---------------------------------------------------------------------------
// Synodic period
// ---------------------------------------------------------------------------

ORCA_TEST(Phasing, SynodicPeriodMatchesReference) {
    // Independently computed: 91518.8277 s = 25.4219 h for 500 km vs 800 km.
    // This is the number the whole ORCA trade-off rests on (REPOSITORY_AUDIT.md §4.2).
    const Seconds tSyn =
        phasing::synodicPeriodSec(orbitAt(500.0, 0.0), orbitAt(800.0, 0.0));
    CHECK_NEAR(tSyn, 91518.8277, 1e-2);
    CHECK_NEAR(tSyn / 3600.0, 25.4219, 1e-3);
}

ORCA_TEST(Phasing, SynodicPeriodIsSymmetric) {
    CHECK_NEAR(phasing::synodicPeriodSec(orbitAt(500.0, 0.0), orbitAt(800.0, 0.0)),
               phasing::synodicPeriodSec(orbitAt(800.0, 0.0), orbitAt(500.0, 0.0)), 1e-6);
}

ORCA_TEST(Phasing, SynodicPeriodDivergesAsOrbitsConverge) {
    // Two nearly-identical orbits drift apart so slowly that they are effectively
    // unreachable from one another. This is why near-co-altitude pairs must be rejected
    // rather than assigned an enormous wait.
    double previous = 0.0;
    for (const double gap : {1000.0, 100.0, 10.0, 1.0, 0.1}) {
        const double tSyn =
            phasing::synodicPeriodSec(orbitAt(500.0, 0.0), orbitAt(500.0 + gap, 0.0));
        CHECK(tSyn > previous);
        previous = tSyn;
    }
    // 0.1 km of separation already implies a synodic period of years.
    CHECK(previous > 365.0 * 86400.0);
}

ORCA_TEST(Phasing, IdenticalOrbitsHaveInfiniteSynodicPeriod) {
    const Seconds tSyn =
        phasing::synodicPeriodSec(orbitAt(500.0, 0.0), orbitAt(500.0, 1.0));
    CHECK(std::isinf(tSyn));
}

// ---------------------------------------------------------------------------
// Required lead angle
// ---------------------------------------------------------------------------

ORCA_TEST(Phasing, RequiredLeadAngleMatchesReference) {
    // alpha = pi - omega_target * coast. Independently computed as 5.612553 deg for
    // a 500 -> 800 km Hohmann.
    const auto window = phasing::nextWindow(orbitAt(500.0, 0.0), orbitAt(800.0, 0.0), 0.0,
                                            coastFor(500.0, 800.0), kPi);
    CHECK_NEAR(radToDeg(window.requiredLeadRad), 5.612553, 1e-4);
}

ORCA_TEST(Phasing, RequiredLeadAngleForGeoTransfer) {
    // Independently computed: 100.657334 deg for 300 km -> GEO.
    const Kilometres geoAltitude = 42164.0 - kEarthRadiusKm;
    const auto window =
        phasing::nextWindow(orbitAt(300.0, 0.0), orbitAt(geoAltitude, 0.0), 0.0,
                            coastFor(300.0, geoAltitude), kPi);
    CHECK_NEAR(radToDeg(window.requiredLeadRad), 100.657334, 1e-3);
}

ORCA_TEST(Phasing, ZeroCoastGivesLeadEqualToTransferArc) {
    // With no coast time the target cannot drift, so it must already be exactly at the
    // arrival point. For a pure plane change (arc 0) that means zero relative phase.
    const auto window =
        phasing::nextWindow(orbitAt(500.0, 0.0), orbitAt(800.0, 0.0), 0.0, 0.0, 0.0);
    CHECK_NEAR(window.requiredLeadRad, 0.0, 1e-12);
}

// ---------------------------------------------------------------------------
// Window search
// ---------------------------------------------------------------------------

ORCA_TEST(Phasing, WaitIsZeroWhenGeometryIsAlreadyCorrect) {
    // Construct the target already at the required lead angle, then confirm the model
    // asks for no wait at all.
    const Orbit source = orbitAt(500.0, 0.0);
    const Seconds coast = coastFor(500.0, 800.0);
    const Radians alpha =
        phasing::nextWindow(source, orbitAt(800.0, 0.0), 0.0, coast, kPi).requiredLeadRad;

    const Orbit aligned = orbitAt(800.0, alpha);
    const auto window = phasing::nextWindow(source, aligned, 0.0, coast, kPi);
    CHECK(window.exists);
    CHECK_NEAR(window.waitSec, 0.0, 1e-6);
}

ORCA_TEST(Phasing, WaitNeverExceedsOneSynodicPeriod) {
    // The geometry repeats every synodic period, so you never wait longer than one.
    const Orbit source = orbitAt(500.0, 0.0);
    const Seconds coast = coastFor(500.0, 800.0);
    const Seconds tSyn = phasing::synodicPeriodSec(source, orbitAt(800.0, 0.0));

    for (double phase = 0.0; phase < kTwoPi; phase += 0.2) {
        const auto window =
            phasing::nextWindow(source, orbitAt(800.0, phase), 0.0, coast, kPi);
        CHECK(window.exists);
        CHECK(window.waitSec >= 0.0);
        CHECK(window.waitSec <= tSyn + 1e-6);
    }
}

ORCA_TEST(Phasing, WindowTimeIsDepartureplusWait) {
    const auto window = phasing::nextWindow(orbitAt(500.0, 0.0), orbitAt(800.0, 2.0),
                                            1234.0, coastFor(500.0, 800.0), kPi);
    CHECK_NEAR(window.windowTimeSec, 1234.0 + window.waitSec, 1e-9);
}

ORCA_TEST(Phasing, WaitDecreasesAtRateOneBetweenWindows) {
    // The core mechanism behind FIFO: as departure advances by dt, the remaining wait
    // shrinks by exactly dt, so the absolute window time stays put. This is what makes
    // arrival a flat staircase rather than a rising line.
    const Orbit source = orbitAt(500.0, 0.0);
    const Orbit target = orbitAt(800.0, 2.0);
    const Seconds coast = coastFor(500.0, 800.0);

    const auto first = phasing::nextWindow(source, target, 0.0, coast, kPi);
    const double dt = 600.0;
    if (first.waitSec > dt + 1.0) {  // stay within the same window
        const auto later = phasing::nextWindow(source, target, dt, coast, kPi);
        CHECK_NEAR(later.waitSec, first.waitSec - dt, 1e-6);
        CHECK_NEAR(later.windowTimeSec, first.windowTimeSec, 1e-6);
    }
}

ORCA_TEST(Phasing, WindowRecursAfterExactlyOneSynodicPeriod) {
    const Orbit source = orbitAt(500.0, 0.0);
    const Orbit target = orbitAt(800.0, 2.0);
    const Seconds coast = coastFor(500.0, 800.0);
    const Seconds tSyn = phasing::synodicPeriodSec(source, target);

    const auto first = phasing::nextWindow(source, target, 0.0, coast, kPi);
    // Depart just after the window closes; the next opportunity is one synodic period on.
    const auto next =
        phasing::nextWindow(source, target, first.windowTimeSec + 1.0, coast, kPi);
    CHECK_NEAR(next.windowTimeSec, first.windowTimeSec + tSyn, 1.0);
}

ORCA_TEST(Phasing, SameOrbitDifferentPhaseHasNoWindow) {
    // Two spacecraft in the same orbit at different phases can never rendezvous by
    // waiting -- there is no relative drift to close the gap. Catching up requires
    // changing orbit, which this model does not offer. Physically correct, and the reason
    // PhasingUnavailable exists as a distinct failure reason.
    const auto window = phasing::nextWindow(orbitAt(500.0, 0.0), orbitAt(500.0, 1.0), 0.0,
                                            0.0, 0.0);
    CHECK(!window.exists);
    CHECK(std::isinf(window.synodicPeriodSec));
}

ORCA_TEST(Phasing, SameOrbitSamePhaseHasAnImmediateWindow) {
    // The degenerate case that *does* work: the two are already co-located.
    const auto window = phasing::nextWindow(orbitAt(500.0, 1.5), orbitAt(500.0, 1.5), 0.0,
                                            0.0, 0.0);
    CHECK(window.exists);
    CHECK_NEAR(window.waitSec, 0.0, 1e-12);
}

ORCA_TEST(Phasing, PhaseWrapAroundIsHandled) {
    // Phases near the 0 / 2*pi boundary must not produce a negative or doubled wait.
    const Orbit source = orbitAt(500.0, 0.0);
    const Seconds coast = coastFor(500.0, 800.0);
    const double phases[] = {0.0, 1e-9, kTwoPi - 1e-9, kTwoPi - 1e-6, 1e-6};
    for (const double phase : phases) {
        const auto window =
            phasing::nextWindow(source, orbitAt(800.0, phase), 0.0, coast, kPi);
        CHECK(window.exists);
        CHECK(window.waitSec >= 0.0);
        CHECK(std::isfinite(window.waitSec));
    }
}

ORCA_TEST(Phasing, WaitIsAlwaysNonNegativeAcrossDepartureTimes) {
    const Orbit source = orbitAt(500.0, 0.7);
    const Orbit target = orbitAt(1200.0, 3.1);
    const Seconds coast = coastFor(500.0, 1200.0);
    for (double t = 0.0; t < 200000.0; t += 613.0) {
        const auto window = phasing::nextWindow(source, target, t, coast, kPi);
        CHECK(window.exists);
        CHECK(window.waitSec >= 0.0);
        CHECK(window.windowTimeSec >= t - 1e-9);
    }
}

ORCA_TEST(Phasing, DescendingTransferAlsoFindsWindows) {
    // Drift direction reverses when the target orbit is lower than the source, which
    // exercises the opposite branch of the gap calculation.
    const Orbit source = orbitAt(1200.0, 0.4);
    const Orbit target = orbitAt(400.0, 2.2);
    const Seconds coast = coastFor(1200.0, 400.0);
    const Seconds tSyn = phasing::synodicPeriodSec(source, target);
    for (double t = 0.0; t < 100000.0; t += 977.0) {
        const auto window = phasing::nextWindow(source, target, t, coast, kPi);
        CHECK(window.exists);
        CHECK(window.waitSec >= 0.0);
        CHECK(window.waitSec <= tSyn + 1e-6);
    }
}
