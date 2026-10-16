/// Tests for Emergency and DebrisObject.

#include "../../cpp/domain/DebrisObject.hpp"
#include "../../cpp/domain/Emergency.hpp"
#include "../../cpp/domain/Orbit.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {

Orbit leoOrbit() {
    return Orbit::fromAltitude(500.0, 0.9, 0.0, 0.0);
}

}  // namespace

// ---------------------------------------------------------------------------
// Emergency
// ---------------------------------------------------------------------------

ORCA_TEST(Emergency, DeadlineIsDerivedNotStored) {
    const Emergency e{EmergencyId{3},        SatelliteId{17},
                      EmergencyKind::PropulsionFailure, Severity::Critical,
                      412.0,                 1920.0};
    // API_CONTRACT.md §4.2 example: detected 412.0, window 1920 -> deadline 2332.0.
    CHECK_NEAR(e.deadlineSec(), 2332.0, 1e-12);
}

ORCA_TEST(Emergency, RejectsInvalidConstruction) {
    CHECK_THROWS(Emergency(EmergencyId{1}, SatelliteId{1}, EmergencyKind::PowerFailure,
                           Severity::Low, 0.0, 0.0));
    CHECK_THROWS(Emergency(EmergencyId{1}, SatelliteId{1}, EmergencyKind::PowerFailure,
                           Severity::Low, 0.0, -100.0));
    CHECK_THROWS(Emergency(EmergencyId{1}, SatelliteId{1}, EmergencyKind::PowerFailure,
                           Severity::Low, -1.0, 100.0));
}

ORCA_TEST(Emergency, RemainingWindowCountsDownAndClampsAtZero) {
    const Emergency e{EmergencyId{3},       SatelliteId{17},
                      EmergencyKind::PropulsionFailure, Severity::Critical,
                      412.0,                1920.0};
    CHECK_NEAR(e.remainingWindowSec(412.0), 1920.0, 1e-12);
    // API_CONTRACT.md §4.2: at t=823.2 the remaining window is 1508.8 s.
    CHECK_NEAR(e.remainingWindowSec(823.2), 1508.8, 1e-9);
    CHECK_NEAR(e.remainingWindowSec(2332.0), 0.0, 1e-12);
    CHECK_NEAR(e.remainingWindowSec(9999.0), 0.0, 1e-12);  // clamped, never negative
}

ORCA_TEST(Emergency, UrgencyRisesFromZeroToOne) {
    const Emergency e{EmergencyId{1},  SatelliteId{1}, EmergencyKind::PowerFailure,
                      Severity::High,  1000.0,         100.0};
    CHECK_NEAR(e.urgencyAt(1000.0), 0.0, 1e-12);   // at detection
    CHECK_NEAR(e.urgencyAt(1050.0), 0.5, 1e-12);   // halfway
    CHECK_NEAR(e.urgencyAt(1100.0), 1.0, 1e-12);   // at deadline
    CHECK_NEAR(e.urgencyAt(5000.0), 1.0, 1e-12);   // past deadline, clamped
}

ORCA_TEST(Emergency, UrgencyIsMonotonicAndBounded) {
    const Emergency e{EmergencyId{1},  SatelliteId{1}, EmergencyKind::Thermal,
                      Severity::Moderate, 0.0,         500.0};
    double previous = -1.0;
    for (double t = 0.0; t <= 1000.0; t += 7.0) {
        const double u = e.urgencyAt(t);
        CHECK(u >= 0.0);
        CHECK(u <= 1.0);
        CHECK(u >= previous - 1e-15);
        previous = u;
    }
}

ORCA_TEST(Emergency, PastDeadlineIsATimeQuestionNotAStateQuestion) {
    const Emergency e{EmergencyId{1}, SatelliteId{1}, EmergencyKind::CommsLoss,
                      Severity::Low,  100.0,          50.0};
    CHECK(!e.isPastDeadline(150.0));  // exactly at the deadline is not past it
    CHECK(e.isPastDeadline(150.1));
    // The state has not changed just because the time has.
    CHECK(e.state() == EmergencyState::Pending);
}

ORCA_TEST(Emergency, TransitionTable) {
    using E = EmergencyState;
    CHECK(isValidTransition(E::Pending, E::Assigned));
    CHECK(isValidTransition(E::Pending, E::Unreachable));
    CHECK(isValidTransition(E::Assigned, E::InProgress));
    CHECK(isValidTransition(E::Assigned, E::Pending));      // reassignment
    CHECK(isValidTransition(E::InProgress, E::Resolved));
    CHECK(isValidTransition(E::InProgress, E::Pending));    // mission aborted
    // Unreachable is not terminal: a vehicle finishing elsewhere may bring the casualty
    // back into range.
    CHECK(isValidTransition(E::Unreachable, E::Pending));
    // Any non-resolved state can expire.
    CHECK(isValidTransition(E::Pending, E::Expired));
    CHECK(isValidTransition(E::InProgress, E::Expired));
    CHECK(!isValidTransition(E::Resolved, E::Expired));
    // Terminal states.
    CHECK(!isValidTransition(E::Resolved, E::Pending));
    CHECK(!isValidTransition(E::Expired, E::Pending));
}

ORCA_TEST(Emergency, StateTransitionsAreEnforced) {
    Emergency e{EmergencyId{1}, SatelliteId{1}, EmergencyKind::FuelCritical,
                Severity::High, 0.0,            100.0};
    CHECK_THROWS(e.setState(EmergencyState::Resolved));  // cannot skip to resolved
    CHECK_NO_THROW(e.setState(EmergencyState::Assigned));
    CHECK_NO_THROW(e.setState(EmergencyState::InProgress));
    CHECK_NO_THROW(e.setState(EmergencyState::Resolved));
    CHECK(e.isTerminal());
    CHECK_THROWS(e.setState(EmergencyState::Pending));
}

// ---------------------------------------------------------------------------
// DebrisObject
// ---------------------------------------------------------------------------

ORCA_TEST(Debris, ConstructsUndetectedByDefault) {
    const DebrisObject d{DebrisId{4}, leoOrbit(), 18.0, 0.7};
    CHECK(!d.isDetected());
    CHECK_NEAR(d.hazardRadiusKm(), 18.0, 1e-15);
    CHECK_NEAR(d.densityFactor(), 0.7, 1e-15);
}

ORCA_TEST(Debris, DetectionIsOneWay) {
    // The mechanism behind dynamic replanning: debris exists from scenario load but is
    // invisible to the planner until detected.
    DebrisObject d{DebrisId{4}, leoOrbit(), 18.0, 0.7};
    d.markDetected();
    CHECK(d.isDetected());
    d.markDetected();
    CHECK(d.isDetected());  // idempotent, and there is no way back
}

ORCA_TEST(Debris, RejectsInvalidConstruction) {
    CHECK_THROWS(DebrisObject(DebrisId{1}, leoOrbit(), 0.0, 0.5));
    CHECK_THROWS(DebrisObject(DebrisId{1}, leoOrbit(), -5.0, 0.5));
    CHECK_THROWS(DebrisObject(DebrisId{1}, leoOrbit(), 10.0, 1.5));
    CHECK_THROWS(DebrisObject(DebrisId{1}, leoOrbit(), 10.0, -0.1));
}

ORCA_TEST(Debris, PositionIsDerivedFromOrbit) {
    const DebrisObject d{DebrisId{4}, leoOrbit(), 18.0, 0.7};
    CHECK(d.positionAtKm(500.0) == d.orbit().positionAtKm(500.0));
}
