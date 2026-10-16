/// `SimulationEngine` integration tests — Phase 8 brief §53(engine-level), §66(reset),
/// §81-84(large-dt, event burst, long-run, replan storm), §55(replay).

#include "../../cpp/domain/Scenario.hpp"
#include "../../cpp/simulation/SimulationEngine.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {

/// The same single-vehicle, single-target scenario `probe_sim1.cpp` verified during
/// development: a fresh 5.0km/s vehicle reaches a 550km/6deg/5deg/0.1rad target with
/// delta-v 0.1467km/s -- reused across several tests below as a small, fast, already
/// cross-verified fixture (matches Phase 7's own `mission_validity_test.cpp` numbers).
Scenario buildSingleVehicleScenario(Seed seed = Seed(12345)) {
    const Orbit satOrbit = Orbit::fromAltitude(550.0, degToRad(6.0), degToRad(5.0), degToRad(0.1));
    const Orbit v1o = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, 0.0);

    std::vector<Satellite> sats;
    sats.emplace_back(SatelliteId(1), "SAT-001", satOrbit);
    std::vector<RescueVehicle> vehicles;
    vehicles.emplace_back(VehicleId(1), "RV-01", v1o, 5.0);

    SimulationConfig config;
    config.timeStepSec = 1.0;
    config.durationSec = 600000.0;
    return Scenario("single-vehicle", seed, config, sats, vehicles, {});
}

Event scriptedFailureAt(Seconds t, SatelliteId satellite) {
    return Event{
        .id = EventId(0),
        .timestampSec = t,
        .kind = EventKind::SatelliteFailure,
        .satellite = satellite,
        .emergencyKind = EmergencyKind::PropulsionFailure,
        .severity = Severity::High,
        .survivalWindowSec = 5.0e7,
        .detail = "scripted",
    };
}

}  // namespace

// ---------------------------------------------------------------------------
// Reset (brief §66).
// ---------------------------------------------------------------------------

ORCA_TEST(SimulationEngine, ResetRestoresExactInitialState) {
    SimulationEngine engine(buildSingleVehicleScenario());
    engine.start();
    engine.scheduleEvent(scriptedFailureAt(30.0, SatelliteId(1)));
    engine.advance(100.0);
    CHECK(!engine.state().missions.empty());

    engine.reset();
    CHECK_NEAR(engine.clock().currentTimeSec(), 0.0, 1e-12);
    CHECK(!engine.clock().isRunning());
    CHECK(engine.state().missions.empty());
    CHECK(engine.state().emergencies.empty());
    CHECK_EQ(engine.state().metrics.ticks, 0L);
    CHECK(engine.eventQueue().empty());
    CHECK_NEAR(engine.state().findVehicle(VehicleId(1))->deltaVRemainingKms(), 5.0, 1e-9);
    CHECK(engine.replanner().history().empty());
}

ORCA_TEST(SimulationEngine, ResetThenRerunProducesTheSameOutcomeAsAFreshEngine) {
    SimulationEngine engineA(buildSingleVehicleScenario());
    engineA.start();
    engineA.scheduleEvent(scriptedFailureAt(30.0, SatelliteId(1)));
    engineA.advance(600000.0);
    const long successesA = engineA.state().metrics.missionSuccesses;

    engineA.reset();
    engineA.start();
    engineA.scheduleEvent(scriptedFailureAt(30.0, SatelliteId(1)));
    engineA.advance(600000.0);

    CHECK_EQ(engineA.state().metrics.missionSuccesses, successesA);
    CHECK_EQ(successesA, 1L);
}

// ---------------------------------------------------------------------------
// Replay determinism (brief §51, §55).
// ---------------------------------------------------------------------------

ORCA_TEST(SimulationEngine, SameSeedAndScriptProduceIdenticalOutcomes) {
    SimulationEngine engineA(buildSingleVehicleScenario(Seed(999)));
    SimulationEngine engineB(buildSingleVehicleScenario(Seed(999)));
    engineA.start();
    engineB.start();
    engineA.scheduleEvent(scriptedFailureAt(30.0, SatelliteId(1)));
    engineB.scheduleEvent(scriptedFailureAt(30.0, SatelliteId(1)));
    engineA.advance(600000.0);
    engineB.advance(600000.0);

    CHECK_EQ(engineA.state().metrics.missionSuccesses, engineB.state().metrics.missionSuccesses);
    CHECK_EQ(engineA.state().metrics.ticks, engineB.state().metrics.ticks);
    CHECK_EQ(engineA.state().metrics.eventsProcessed, engineB.state().metrics.eventsProcessed);
    CHECK_EQ(engineA.state().missions.size(), engineB.state().missions.size());
    const Mission* missionA = engineA.state().findMission(MissionId(0));
    const Mission* missionB = engineB.state().findMission(MissionId(0));
    CHECK(missionA->state() == missionB->state());
    CHECK_NEAR(missionA->deltaVSpentKms(), missionB->deltaVSpentKms(), 1e-12);
    CHECK(missionA->completedAtSec() == missionB->completedAtSec());
}

// ---------------------------------------------------------------------------
// Large timestep (brief §81): a single big advance() must process every intermediate
// event, matching many small steps exactly.
// ---------------------------------------------------------------------------

ORCA_TEST(SimulationEngine, LargeAdvanceProcessesEveryEventNoneSkipped) {
    SimulationEngine engineOneJump(buildSingleVehicleScenario());
    engineOneJump.start();
    engineOneJump.scheduleEvent(scriptedFailureAt(30.0, SatelliteId(1)));
    engineOneJump.advance(1000.0);  // one big jump

    SimulationEngine engineManySteps(buildSingleVehicleScenario());
    engineManySteps.start();
    engineManySteps.scheduleEvent(scriptedFailureAt(30.0, SatelliteId(1)));
    for (int i = 0; i < 1000; ++i) engineManySteps.step();  // a thousand individual steps

    CHECK_EQ(engineOneJump.state().metrics.eventsProcessed,
            engineManySteps.state().metrics.eventsProcessed);
    CHECK_EQ(engineOneJump.state().missions.size(), engineManySteps.state().missions.size());
    CHECK_EQ(engineOneJump.state().emergencies.size(), engineManySteps.state().emergencies.size());
    CHECK(engineOneJump.state().metrics.eventsProcessed > 0);
}

// ---------------------------------------------------------------------------
// Event burst (brief §82): many events at the same/nearby times, no loss, no duplicates.
// ---------------------------------------------------------------------------

ORCA_TEST(SimulationEngine, EventBurstAtTheSameTimestampAllProcessExactlyOnce) {
    SimulationEngine engine(buildSingleVehicleScenario());
    engine.start();
    // Five distinct, harmless (non-mutating-state-machine-conflicting) events at the exact
    // same timestamp -- VehicleStateChange is a pure notification kind (EventDispatcher.cpp
    // treats it as a no-op), safe to burst without needing five distinct satellites/vehicles.
    for (int i = 0; i < 5; ++i) {
        engine.scheduleEvent(Event{
            .id = EventId(0),
            .timestampSec = 50.0,
            .kind = EventKind::VehicleStateChange,
            .vehicle = VehicleId(1),
            .detail = "burst",
        });
    }
    engine.advance(100.0);
    CHECK_EQ(engine.state().metrics.eventsProcessed, 5L);
    CHECK(engine.eventQueue().empty());
}

// ---------------------------------------------------------------------------
// Long run (brief §83): no NaN, no invalid state, no negative time, queue stays sane.
// ---------------------------------------------------------------------------

ORCA_TEST(SimulationEngine, LongRunProducesNoInvalidState) {
    SimulationEngine engine(buildSingleVehicleScenario());
    engine.start();
    engine.scheduleEvent(scriptedFailureAt(30.0, SatelliteId(1)));
    engine.advance(600000.0);

    CHECK(engine.state().simulationTimeSec >= 0.0);
    CHECK(!std::isnan(engine.state().simulationTimeSec));
    for (const auto& [id, vehicle] : engine.state().vehicles) {
        CHECK(vehicle.deltaVRemainingKms() >= 0.0);
        CHECK(!std::isnan(vehicle.deltaVRemainingKms()));
    }
    for (const auto& [id, mission] : engine.state().missions) {
        CHECK(mission.deltaVSpentKms() >= 0.0);
        CHECK(mission.progressOnLeg() >= 0.0 && mission.progressOnLeg() <= 1.0);
    }
    CHECK(engine.state().metrics.ticks == 600000);
    // The queue does not accumulate unboundedly across a long, otherwise-quiet run --
    // every scheduled event was eventually drained.
    CHECK(engine.eventQueue().size() < 100);
}

// ---------------------------------------------------------------------------
// Replan storm (brief §84): many material changes on the same mission must not thrash --
// hysteresis (switchMargin, throttling) keeps the outcome bounded and coherent.
// ---------------------------------------------------------------------------

ORCA_TEST(SimulationEngine, RepeatedIdenticalTriggersDoNotCauseUnboundedReplanning) {
    SimulationEngine engine(buildSingleVehicleScenario());
    engine.start();
    engine.scheduleEvent(scriptedFailureAt(5.0, SatelliteId(1)));
    engine.advance(20.0);

    CHECK_EQ(engine.state().missions.size(), std::size_t{1});
    const MissionId missionId(0);

    // Fire the same "communication lost" trigger many times in a row -- with no actual
    // change in the underlying route/resources between calls, none of these should find a
    // materially better alternative to switch to (there is only ever one route on this
    // single-edge graph), so replanCount must stay at zero, not grow with each trigger.
    for (int i = 0; i < 20; ++i) {
        engine.scheduleEvent(Event{
            .id = EventId(0),
            .timestampSec = 25.0 + static_cast<double>(i),
            .kind = EventKind::CommunicationLoss,
            .vehicle = VehicleId(1),
            .detail = "repeated comm loss",
        });
    }
    engine.advance(50.0);

    const Mission* mission = engine.state().findMission(missionId);
    CHECK(mission != nullptr);
    CHECK(mission->state() == MissionState::InTransit);
    CHECK_EQ(mission->replanCount(), 0);  // nothing to switch TO -- correctly retained, every time
}
