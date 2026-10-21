/// The canonical Phase 8 end-to-end demo scenario — brief §85-86: a satellite fails, ORCA
/// plans and dispatches a rescue, a debris hazard invalidates the in-progress route,
/// `DynamicReplanner` finds and adopts a real alternative, and the mission completes.
///
/// Every number and state transition below came from actually compiling and running this
/// exact scenario (`probe_sim5.cpp` during development) — nothing is asserted from
/// hand-derivation. The scenario is smaller than brief §24's illustrative "24 satellites, 3
/// vehicles" -- three satellites (two waypoints plus the casualty) and one vehicle is the
/// smallest topology that genuinely exercises every stage of the chain (global replan,
/// local replan with a real alternative, multi-leg execution, resource consumption,
/// rendezvous) without requiring 24 hand-verified orbits; `SIMULATION_ENGINE.md` states
/// this scope decision explicitly.

#include "../../cpp/domain/Scenario.hpp"
#include "../../cpp/simulation/DemoScenario.hpp"
#include "../../cpp/simulation/SimulationEngine.hpp"
#include "../TestFramework.hpp"

using namespace orca;

// `buildDemoScenario()` now lives in `cpp/simulation/DemoScenario.{hpp,cpp}` (Phase 9), so
// this test and the `--serve` CLI share exactly one definition rather than two copies that
// could silently drift apart.

ORCA_TEST(EndToEndDemo, PropulsionFailureThroughDebrisRerouteToRescueComplete) {
    SimulationEngine engine(buildDemoScenario());
    engine.start();

    // T=5: SAT-101 fails.
    engine.scheduleEvent(Event{
        .id = EventId(0),
        .timestampSec = 5.0,
        .kind = EventKind::SatelliteFailure,
        .satellite = SatelliteId(101),
        .emergencyKind = EmergencyKind::PowerFailure,
        .severity = Severity::High,
        .survivalWindowSec = 5.0e7,
        .detail = "scripted failure",
    });
    engine.advance(20.0);

    // ORCA plans and dispatches: exactly one emergency, one mission, in transit, on a
    // real 2-leg route (no direct edge exists -- the same-period lock above).
    CHECK_EQ(engine.state().emergencies.size(), std::size_t{1});
    CHECK_EQ(engine.state().missions.size(), std::size_t{1});
    const MissionId missionId(0);
    const Mission* mission = engine.state().findMission(missionId);
    CHECK(mission != nullptr);
    CHECK(mission->state() == MissionState::InTransit);
    CHECK_EQ(mission->currentRoute().size(), std::size_t{2});
    CHECK_EQ(mission->vehicle().value(), std::uint32_t{1});
    CHECK_EQ(mission->emergency().value(), std::uint32_t{0});

    // T=25: debris detected, threatening the currently-adopted route through MID-A.
    engine.scheduleEvent(Event{
        .id = EventId(0),
        .timestampSec = 25.0,
        .kind = EventKind::DebrisAlert,
        .debris = DebrisId(1),
        .detail = "debris detected",
    });
    engine.advance(10.0);

    // DynamicReplanner found and adopted a real alternative -- a mandatory replan, not an
    // abort, because MID-B's route survives the exclusion.
    mission = engine.state().findMission(missionId);
    CHECK(mission->state() == MissionState::InTransit);
    CHECK_EQ(mission->replanCount(), 1);
    CHECK_EQ(mission->rejectedAlternatives().size(), std::size_t{1});
    CHECK_EQ(engine.replanner().history().size(), std::size_t{1});
    CHECK(engine.replanner().history()[0].reason == ReplanReason::RouteInvalidated);
    CHECK(engine.replanner().history()[0].decision == ReplanDecisionType::Replan);

    // Advance to actual completion.
    engine.advance(600000.0 - engine.state().simulationTimeSec);

    mission = engine.state().findMission(missionId);
    CHECK(mission->state() == MissionState::Success);
    CHECK(mission->completedAtSec().has_value());

    const Emergency* emergency = engine.state().findEmergency(EmergencyId(0));
    CHECK(emergency->state() == EmergencyState::Resolved);

    const Satellite* satellite = engine.state().findSatellite(SatelliteId(101));
    CHECK(satellite->status() == SatelliteStatus::Rescued);
    CHECK(!satellite->activeEmergency().has_value());

    const RescueVehicle* vehicle = engine.state().findVehicle(VehicleId(1));
    CHECK(vehicle->status() == VehicleStatus::Idle);
    CHECK(!vehicle->activeMission().has_value());
    // 10.0 km/s budget minus all three legs actually flown: the in-progress leg to MID-A
    // (0.436256), then MID-A -> MID-B (0.288446) and MID-B -> casualty (0.720524).
    // This used to read 8.9910, which was the in-progress leg's delta-v silently dropped by
    // the replan (see replan_commitment_test.cpp).
    CHECK_NEAR(vehicle->deltaVRemainingKms(), 8.5548, 1e-3);
    CHECK_EQ(mission->currentRoute().size(), std::size_t{3});

    CHECK_EQ(engine.state().metrics.missionSuccesses, 1L);
    CHECK_EQ(engine.state().metrics.replansTriggered, 1L);
    CHECK_EQ(engine.state().metrics.replansCompleted, 1L);
    CHECK_EQ(engine.state().metrics.missionAborts, 0L);
    CHECK_EQ(engine.state().metrics.missionFailures, 0L);

    // The full chain, in the event history's own recorded order: satellite failure ->
    // emergency detection -> mission dispatch -> debris -> replan -> rendezvous ->
    // complete. Not every step schedules a distinct Event (mission dispatch happens
    // inline within the SatelliteFailure handler), but every EVENT that WAS scheduled is
    // still in the (already-drained) queue's processing history via eventsProcessed.
    CHECK(engine.state().metrics.eventsProcessed >= 6);
}
