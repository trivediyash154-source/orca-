/// Tests for RescueVehicle.

#include "../../cpp/domain/Orbit.hpp"
#include "../../cpp/domain/RescueVehicle.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {

Orbit leoOrbit() {
    return Orbit::fromAltitude(500.0, 0.9, 0.0, 0.0);
}

RescueVehicle idleVehicle() {
    return RescueVehicle{VehicleId{2}, "Aegis-2", leoOrbit(), 2.5};
}

}  // namespace

ORCA_TEST(RescueVehicle, ConstructsFullyFuelled) {
    const RescueVehicle rv = idleVehicle();
    CHECK(rv.status() == VehicleStatus::Idle);
    CHECK_NEAR(rv.deltaVBudgetKms(), 2.5, 1e-15);
    CHECK_NEAR(rv.deltaVRemainingKms(), 2.5, 1e-15);
    CHECK_NEAR(rv.fuelFraction(), 1.0, 1e-15);
    CHECK(rv.isAvailable());
}

ORCA_TEST(RescueVehicle, RejectsInvalidConstruction) {
    CHECK_THROWS(RescueVehicle(VehicleId{1}, "", leoOrbit(), 2.5));
    CHECK_THROWS(RescueVehicle(VehicleId{1}, "V", leoOrbit(), 0.0));
    CHECK_THROWS(RescueVehicle(VehicleId{1}, "V", leoOrbit(), -1.0));
    CHECK_THROWS(RescueVehicle(VehicleId{1}, "V", leoOrbit(), 2.5, 0));
}

ORCA_TEST(RescueVehicle, FuelFractionMatchesApiContractExample) {
    // API_CONTRACT.md §4.2 shows RV-02 with budget 2.5, remaining 2.16, fuelFraction
    // 0.864. This pins the definition as remaining/budget, not consumed/budget -- the
    // conflation DATA_MODEL.md §3.2 originally carried.
    RescueVehicle rv = idleVehicle();
    rv.consumeDeltaV(0.34);
    CHECK_NEAR(rv.deltaVRemainingKms(), 2.16, 1e-12);
    CHECK_NEAR(rv.fuelFraction(), 0.864, 1e-12);
}

ORCA_TEST(RescueVehicle, DeltaVDecreasesMonotonically) {
    RescueVehicle rv = idleVehicle();
    double previous = rv.deltaVRemainingKms();
    for (int i = 0; i < 20; ++i) {
        rv.consumeDeltaV(0.1);
        CHECK(rv.deltaVRemainingKms() <= previous);
        previous = rv.deltaVRemainingKms();
    }
    CHECK_NEAR(rv.deltaVRemainingKms(), 0.5, 1e-12);
}

ORCA_TEST(RescueVehicle, RejectsOverspend) {
    // Overspending is rejected rather than clamped: a vehicle that silently completed an
    // unaffordable transfer would make the feasibility guarantee meaningless.
    RescueVehicle rv = idleVehicle();
    CHECK_THROWS(rv.consumeDeltaV(3.0));
    CHECK_NEAR(rv.deltaVRemainingKms(), 2.5, 1e-15);  // unchanged
}

ORCA_TEST(RescueVehicle, RejectsNegativeSpend) {
    // Refuelling is not modelled, so a negative spend is an error rather than a top-up.
    RescueVehicle rv = idleVehicle();
    CHECK_THROWS(rv.consumeDeltaV(-0.5));
}

ORCA_TEST(RescueVehicle, SpendingExactlyTheBudgetIsAllowed) {
    RescueVehicle rv = idleVehicle();
    CHECK_NO_THROW(rv.consumeDeltaV(2.5));
    CHECK_NEAR(rv.deltaVRemainingKms(), 0.0, 1e-15);
    CHECK_NEAR(rv.fuelFraction(), 0.0, 1e-15);
    CHECK(!rv.isAvailable());  // no delta-v left
}

ORCA_TEST(RescueVehicle, CanAfford) {
    const RescueVehicle rv = idleVehicle();
    CHECK(rv.canAfford(2.5));
    CHECK(rv.canAfford(0.0));
    CHECK(!rv.canAfford(2.51));
}

ORCA_TEST(RescueVehicle, AvailabilityRequiresFreeStatusAndFuel) {
    RescueVehicle rv = idleVehicle();
    CHECK(rv.isAvailable());
    rv.setStatus(VehicleStatus::Assigned);
    CHECK(!rv.isAvailable());
    rv.setStatus(VehicleStatus::Transferring);
    rv.setStatus(VehicleStatus::OnStation);
    CHECK(rv.isAvailable());  // on station and free for onward tasking
    rv.setStatus(VehicleStatus::Disabled);
    CHECK(!rv.isAvailable());
}

ORCA_TEST(RescueVehicle, TransitionTable) {
    using V = VehicleStatus;
    CHECK(isValidTransition(V::Idle, V::Assigned));
    CHECK(!isValidTransition(V::Idle, V::Transferring));
    CHECK(isValidTransition(V::Assigned, V::Transferring));
    CHECK(isValidTransition(V::Assigned, V::Idle));  // cancellation
    CHECK(isValidTransition(V::Transferring, V::OnStation));
    CHECK(isValidTransition(V::OnStation, V::Transferring));
    CHECK(isValidTransition(V::Returning, V::Idle));
    // Anything can fail, once.
    CHECK(isValidTransition(V::Idle, V::Disabled));
    CHECK(isValidTransition(V::Transferring, V::Disabled));
    CHECK(!isValidTransition(V::Disabled, V::Idle));
}

ORCA_TEST(RescueVehicle, RejectsIllegalTransition) {
    RescueVehicle rv = idleVehicle();
    CHECK_THROWS(rv.setStatus(VehicleStatus::Transferring));
    CHECK(rv.status() == VehicleStatus::Idle);
}

ORCA_TEST(RescueVehicle, MissionAssignment) {
    RescueVehicle rv = idleVehicle();
    rv.assignMission(MissionId{1});
    CHECK(rv.activeMission().has_value());
    CHECK(*rv.activeMission() == MissionId{1});
    rv.clearMission();
    CHECK(!rv.activeMission().has_value());
}

ORCA_TEST(RescueVehicle, PositionIsDerivedFromOrbit) {
    const RescueVehicle rv = idleVehicle();
    CHECK(rv.positionAtKm(750.0) == rv.orbit().positionAtKm(750.0));
}
