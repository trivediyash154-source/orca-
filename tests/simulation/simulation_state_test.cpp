/// `SimulationState`/`makeInitialState()` tests — Phase 8 brief §18-19's single-source-of-
/// truth world state.

#include "../../cpp/domain/Scenario.hpp"
#include "../../cpp/simulation/SimulationState.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {
Scenario buildTinyScenario() {
    const Orbit o1 = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, 0.0);
    const Orbit o2 = Orbit::fromAltitude(550.0, degToRad(6.0), 0.0, 0.5);
    std::vector<Satellite> sats;
    sats.emplace_back(SatelliteId(1), "SAT-001", o1);
    sats.emplace_back(SatelliteId(2), "SAT-002", o2);
    std::vector<RescueVehicle> vehicles;
    vehicles.emplace_back(VehicleId(1), "RV-01", o1, 5.0);
    vehicles.emplace_back(VehicleId(2), "RV-02", o2, 5.0);
    std::vector<DebrisObject> debris;
    debris.emplace_back(DebrisId(1), o2, 5.0, 0.5, false);
    SimulationConfig config;
    return Scenario("tiny", Seed(1), config, sats, vehicles, debris);
}
}  // namespace

ORCA_TEST(SimulationState, MakeInitialStateCopiesEveryScenarioEntity) {
    const SimulationState state = makeInitialState(buildTinyScenario());
    CHECK_EQ(state.satellites.size(), std::size_t{2});
    CHECK_EQ(state.vehicles.size(), std::size_t{2});
    CHECK_EQ(state.debris.size(), std::size_t{1});
    CHECK(state.emergencies.empty());
    CHECK(state.missions.empty());
    CHECK_NEAR(state.simulationTimeSec, 0.0, 1e-12);
}

ORCA_TEST(SimulationState, LookupsFindExistingEntitiesAndReturnNullForMissingOnes) {
    SimulationState state = makeInitialState(buildTinyScenario());
    CHECK(state.findSatellite(SatelliteId(1)) != nullptr);
    CHECK(state.findSatellite(SatelliteId(999)) == nullptr);
    CHECK(state.findVehicle(VehicleId(1)) != nullptr);
    CHECK(state.findVehicle(VehicleId(999)) == nullptr);
    CHECK(state.findDebris(DebrisId(1)) != nullptr);
    CHECK(state.findEmergency(EmergencyId(1)) == nullptr);
    CHECK(state.findMission(MissionId(1)) == nullptr);
}

ORCA_TEST(SimulationState, MutableLookupActuallyMutatesStoredEntity) {
    SimulationState state = makeInitialState(buildTinyScenario());
    RescueVehicle* vehicle = state.findVehicle(VehicleId(1));
    vehicle->consumeDeltaV(1.0);
    CHECK_NEAR(state.findVehicle(VehicleId(1))->deltaVRemainingKms(), 4.0, 1e-9);
}

ORCA_TEST(SimulationState, AvailableVehicleIdsReflectsCurrentStatus) {
    SimulationState state = makeInitialState(buildTinyScenario());
    CHECK_EQ(state.availableVehicleIds().size(), std::size_t{2});
    state.findVehicle(VehicleId(1))->setStatus(VehicleStatus::Assigned);
    const auto available = state.availableVehicleIds();
    CHECK_EQ(available.size(), std::size_t{1});
    CHECK_EQ(available[0].value(), std::uint32_t{2});
}

ORCA_TEST(SimulationState, PendingEmergencyIdsOnlyIncludesPendingState) {
    SimulationState state = makeInitialState(buildTinyScenario());
    Emergency e(EmergencyId(0), SatelliteId(1), EmergencyKind::PowerFailure, Severity::High,
               0.0, 1.0e6);
    state.emergencies.emplace(0, e);
    CHECK_EQ(state.pendingEmergencyIds().size(), std::size_t{1});
    state.findEmergency(EmergencyId(0))->setState(EmergencyState::Assigned);
    CHECK(state.pendingEmergencyIds().empty());
}

ORCA_TEST(SimulationState, PlanIdAndEntityIdCountersStartAtDocumentedDefaults) {
    SimulationState state = makeInitialState(buildTinyScenario());
    CHECK_EQ(state.nextEmergencyId, std::uint32_t{0});
    CHECK_EQ(state.nextMissionId, std::uint32_t{0});
    CHECK_EQ(state.nextPlanId.value(), std::uint32_t{1});
}

ORCA_TEST(SimulationState, RecordEventAppendsInOrder) {
    SimulationState state = makeInitialState(buildTinyScenario());
    state.recordEvent(Event{.id = EventId(0), .timestampSec = 1.0, .kind = EventKind::DebrisAlert});
    state.recordEvent(Event{.id = EventId(1), .timestampSec = 2.0, .kind = EventKind::Rendezvous});
    CHECK_EQ(state.recentEvents.size(), std::size_t{2});
    CHECK(state.recentEvents[0].kind == EventKind::DebrisAlert);
    CHECK(state.recentEvents[1].kind == EventKind::Rendezvous);
}

ORCA_TEST(SimulationState, RecordEventTrimsToTheCapOldestFirst) {
    SimulationState state = makeInitialState(buildTinyScenario());
    for (std::uint32_t i = 0; i < SimulationState::kMaxRecentEvents + 10; ++i) {
        state.recordEvent(
            Event{.id = EventId(i), .timestampSec = static_cast<double>(i), .kind = EventKind::DebrisAlert});
    }
    CHECK_EQ(state.recentEvents.size(), SimulationState::kMaxRecentEvents);
    CHECK_EQ(state.recentEvents.front().id.value(), std::uint32_t{10});
    CHECK_EQ(state.recentEvents.back().id.value(), SimulationState::kMaxRecentEvents + 9);
}

ORCA_TEST(SimulationState, MetricsStartAtZero) {
    const SimulationState state = makeInitialState(buildTinyScenario());
    CHECK_EQ(state.metrics.ticks, 0L);
    CHECK_EQ(state.metrics.eventsProcessed, 0L);
    CHECK_EQ(state.metrics.missionSuccesses, 0L);
    CHECK_EQ(state.metrics.missionFailures, 0L);
    CHECK_EQ(state.metrics.missionAborts, 0L);
}
