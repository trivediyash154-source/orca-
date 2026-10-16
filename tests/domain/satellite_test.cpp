/// Tests for Subsystems and Satellite.
///
/// The emphasis is on invariants: what the domain refuses to represent.

#include "../../cpp/domain/Orbit.hpp"
#include "../../cpp/domain/Satellite.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {

Orbit leoOrbit() {
    return Orbit::fromAltitude(500.0, 0.9, 0.0, 0.0);
}

Satellite nominalSatellite() {
    return Satellite{SatelliteId{17}, "Meridian-3", leoOrbit()};
}

}  // namespace

// ---------------------------------------------------------------------------
// Subsystems
// ---------------------------------------------------------------------------

ORCA_TEST(Subsystems, NominalIsFullyHealthy) {
    const Subsystems s = Subsystems::nominal();
    CHECK_NEAR(s.worst(), 1.0, 1e-15);
    CHECK(s.allInUnitInterval());
}

ORCA_TEST(Subsystems, WorstReturnsMinimum) {
    // The minimum is the right aggregate: a spacecraft is only as capable as its weakest
    // system, so a mean would hide a dead subsystem behind three healthy ones.
    const Subsystems s{1.0, 0.2, 0.8, 0.5};
    CHECK_NEAR(s.worst(), 0.2, 1e-15);
}

ORCA_TEST(Subsystems, GetAndSetRoundTrip) {
    Subsystems s = Subsystems::nominal();
    s.set(Subsystem::Comms, 0.42);
    CHECK_NEAR(s.get(Subsystem::Comms), 0.42, 1e-15);
    CHECK_NEAR(s.comms, 0.42, 1e-15);
    CHECK_NEAR(s.get(Subsystem::Power), 1.0, 1e-15);  // others untouched
}

ORCA_TEST(Subsystems, SetClampsToUnitInterval) {
    Subsystems s = Subsystems::nominal();
    s.set(Subsystem::Power, -5.0);
    CHECK_NEAR(s.power, 0.0, 1e-15);
    s.set(Subsystem::Power, 5.0);
    CHECK_NEAR(s.power, 1.0, 1e-15);
}

ORCA_TEST(Subsystems, ValidatedRejectsOutOfRange) {
    CHECK_NO_THROW(Subsystems::validated(1.0, 0.5, 0.0, 0.25));
    CHECK_THROWS(Subsystems::validated(1.1, 1.0, 1.0, 1.0));
    CHECK_THROWS(Subsystems::validated(1.0, -0.1, 1.0, 1.0));
}

// ---------------------------------------------------------------------------
// Satellite
// ---------------------------------------------------------------------------

ORCA_TEST(Satellite, ConstructsNominal) {
    const Satellite sat = nominalSatellite();
    CHECK(sat.id() == SatelliteId{17});
    CHECK_EQ(sat.name(), std::string{"Meridian-3"});
    CHECK(sat.status() == SatelliteStatus::Nominal);
    CHECK(sat.isOperational());
    CHECK(!sat.requiresRescue());
    CHECK(!sat.isTerminal());
    CHECK(!sat.activeEmergency().has_value());
    CHECK_NEAR(sat.fuelFraction(), 1.0, 1e-15);
}

ORCA_TEST(Satellite, RejectsEmptyName) {
    CHECK_THROWS(Satellite(SatelliteId{1}, "", leoOrbit()));
}

ORCA_TEST(Satellite, RejectsOutOfRangeFuel) {
    CHECK_THROWS(Satellite(SatelliteId{1}, "S", leoOrbit(), -0.1));
    CHECK_THROWS(Satellite(SatelliteId{1}, "S", leoOrbit(), 1.1));
    CHECK_NO_THROW(Satellite(SatelliteId{1}, "S", leoOrbit(), 0.0));
    CHECK_NO_THROW(Satellite(SatelliteId{1}, "S", leoOrbit(), 1.0));
}

ORCA_TEST(Satellite, SetFuelFractionValidates) {
    Satellite sat = nominalSatellite();
    CHECK_NO_THROW(sat.setFuelFraction(0.5));
    CHECK_NEAR(sat.fuelFraction(), 0.5, 1e-15);
    CHECK_THROWS(sat.setFuelFraction(1.5));
    CHECK_NEAR(sat.fuelFraction(), 0.5, 1e-15);  // rejected write left it untouched
}

ORCA_TEST(Satellite, PositionIsDerivedFromOrbit) {
    const Satellite sat = nominalSatellite();
    const Vector3 fromSatellite = sat.positionAtKm(1234.0);
    const Vector3 fromOrbit = sat.orbit().positionAtKm(1234.0);
    CHECK(fromSatellite == fromOrbit);
    // No stored position means no way for the two to disagree.
    CHECK_NEAR(magnitude(fromSatellite), sat.orbit().semiMajorAxisKm(), 1e-9);
}

ORCA_TEST(Satellite, StatusDerivedFromInitialHealth) {
    // A scenario that starts a satellite degraded gets a consistent status without having
    // to state it twice.
    const Satellite degraded{SatelliteId{1}, "S", leoOrbit(), 1.0,
                             Subsystems{1.0, 0.5, 1.0, 1.0}};
    CHECK(degraded.status() == SatelliteStatus::Warning);

    const Satellite critical{SatelliteId{2}, "S", leoOrbit(), 1.0,
                             Subsystems{1.0, 0.2, 1.0, 1.0}};
    CHECK(critical.status() == SatelliteStatus::Critical);
}

ORCA_TEST(Satellite, DamageWalksTheStatusLadder) {
    Satellite sat = nominalSatellite();
    CHECK(sat.status() == SatelliteStatus::Nominal);

    sat.applyDamage(Subsystem::Propulsion, 0.5);  // worst = 0.5 -> Warning
    CHECK(sat.status() == SatelliteStatus::Warning);

    sat.applyDamage(Subsystem::Propulsion, 0.3);  // worst = 0.2 -> Critical
    CHECK(sat.status() == SatelliteStatus::Critical);

    sat.applyDamage(Subsystem::Propulsion, 0.2);  // worst = 0.0 -> Failed
    CHECK(sat.status() == SatelliteStatus::Failed);
    CHECK(sat.requiresRescue());
    CHECK(!sat.isOperational());
}

ORCA_TEST(Satellite, CatastrophicDamagePassesThroughIntermediateStates) {
    // Damage large enough to jump straight from Nominal to Failed must still traverse
    // Warning and Critical, so Phase 11 event emission sees every transition.
    Satellite sat = nominalSatellite();
    sat.applyDamage(Subsystem::Power, 1.0);
    CHECK(sat.status() == SatelliteStatus::Failed);
    CHECK_NEAR(sat.health().power, 0.0, 1e-15);
}

ORCA_TEST(Satellite, DamageClampsAtZero) {
    Satellite sat = nominalSatellite();
    sat.applyDamage(Subsystem::Thermal, 10.0);
    CHECK_NEAR(sat.health().thermal, 0.0, 1e-15);
    CHECK(sat.health().allInUnitInterval());
}

ORCA_TEST(Satellite, RejectsNegativeDamage) {
    Satellite sat = nominalSatellite();
    CHECK_THROWS(sat.applyDamage(Subsystem::Power, -0.5));
}

ORCA_TEST(Satellite, RescueTransitionRequiresFailedState) {
    Satellite sat = nominalSatellite();
    CHECK_THROWS(sat.setStatus(SatelliteStatus::Rescued));  // cannot rescue a healthy one

    sat.applyDamage(Subsystem::Power, 1.0);
    CHECK(sat.status() == SatelliteStatus::Failed);
    CHECK_NO_THROW(sat.setStatus(SatelliteStatus::Rescued));
    CHECK(sat.isTerminal());
}

ORCA_TEST(Satellite, TerminalStatesAreFinal) {
    Satellite sat = nominalSatellite();
    sat.applyDamage(Subsystem::Power, 1.0);
    sat.setStatus(SatelliteStatus::Lost);
    CHECK(sat.isTerminal());
    CHECK_THROWS(sat.setStatus(SatelliteStatus::Rescued));
    CHECK_THROWS(sat.setStatus(SatelliteStatus::Nominal));
}

ORCA_TEST(Satellite, TerminalSatelliteIgnoresFurtherDamage) {
    // Damaging a rescued satellite must not resurrect it into the health state machine.
    Satellite sat = nominalSatellite();
    sat.applyDamage(Subsystem::Power, 1.0);
    sat.setStatus(SatelliteStatus::Rescued);
    sat.applyDamage(Subsystem::Comms, 1.0);
    CHECK(sat.status() == SatelliteStatus::Rescued);
}

ORCA_TEST(Satellite, TransitionTableMatchesDocumentedStateMachine) {
    using S = SatelliteStatus;
    CHECK(isValidTransition(S::Nominal, S::Warning));
    CHECK(!isValidTransition(S::Nominal, S::Failed));
    CHECK(!isValidTransition(S::Nominal, S::Rescued));
    CHECK(isValidTransition(S::Warning, S::Nominal));
    CHECK(isValidTransition(S::Warning, S::Critical));
    CHECK(isValidTransition(S::Critical, S::Failed));
    CHECK(isValidTransition(S::Critical, S::Warning));
    CHECK(isValidTransition(S::Failed, S::Rescued));
    CHECK(isValidTransition(S::Failed, S::Lost));
    CHECK(!isValidTransition(S::Rescued, S::Nominal));
    CHECK(!isValidTransition(S::Lost, S::Rescued));
    CHECK(isValidTransition(S::Lost, S::Lost));  // idempotent
}

ORCA_TEST(Satellite, EmergencyAssignment) {
    Satellite sat = nominalSatellite();
    sat.assignEmergency(EmergencyId{3});
    CHECK(sat.activeEmergency().has_value());
    CHECK(*sat.activeEmergency() == EmergencyId{3});
    sat.clearEmergency();
    CHECK(!sat.activeEmergency().has_value());
}
