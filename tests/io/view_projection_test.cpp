/// `buildSnapshot()` tests — Phase 9 brief §52: known simulation state, serialized, checked
/// field by field against values either trivially derivable (a zero-inclination,
/// zero-phase, zero-RAAN orbit's position is exactly `(semiMajorAxisKm, 0, 0)` by
/// `Orbit.hpp`'s own documented formula) or cross-checked against Phase 8's own
/// independently-verified demo scenario (`DemoScenario.hpp`), never against a second,
/// parallel serializer implementation.

#include "../../cpp/io/JsonParser.hpp"
#include "../../cpp/io/JsonSerializer.hpp"
#include "../../cpp/io/ViewProjection.hpp"
#include "../../cpp/simulation/DemoScenario.hpp"
#include "../../cpp/simulation/EmergencyGenerator.hpp"
#include "../TestFramework.hpp"

using namespace orca;
using namespace orca::io;

namespace {
Scenario buildTrivialScenario() {
    // Zero inclination, zero RAAN, zero phase -- position is exactly (a, 0, 0) at t=0 by
    // Orbit.hpp's own formula (cos0=1, sin0=0), not something this test derives itself.
    const Orbit o = Orbit::fromAltitude(500.0, 0.0, 0.0, 0.0);
    std::vector<Satellite> sats;
    sats.emplace_back(SatelliteId(1), "SAT-001", o, 0.75);
    std::vector<RescueVehicle> vehicles;
    vehicles.emplace_back(VehicleId(1), "RV-01", o, 5.0);
    std::vector<DebrisObject> debris;
    debris.emplace_back(DebrisId(1), o, 3.0, 0.4, false);
    SimulationConfig config;
    return Scenario("trivial", Seed(1), config, sats, vehicles, debris);
}
}  // namespace

ORCA_TEST(ViewProjection, SnapshotHasSchemaVersionAndInitialStatus) {
    const SimulationEngine engine(buildTrivialScenario());
    const SimulationSnapshot snap = buildSnapshot(engine);
    CHECK_EQ(snap.schemaVersion, 1);
    CHECK_NEAR(snap.simulationTimeSec, 0.0, 1e-12);
    CHECK_EQ(snap.simulationStatus, std::string("PAUSED"));  // never start()ed
    CHECK_NEAR(snap.speedMultiplier, 1.0, 1e-12);
}

ORCA_TEST(ViewProjection, SatelliteViewMatchesKnownOrbitAndHealth) {
    const SimulationEngine engine(buildTrivialScenario());
    const SimulationSnapshot snap = buildSnapshot(engine);
    CHECK_EQ(snap.satellites.size(), std::size_t{1});
    const SatelliteView& s = snap.satellites[0];
    CHECK_EQ(s.id, std::string("SAT-001"));
    CHECK_EQ(s.name, std::string("SAT-001"));
    CHECK_EQ(s.status, std::string("NOMINAL"));
    CHECK_NEAR(s.position.x, 6878.137, 1e-6);  // altitude 500 + kEarthRadiusKm
    CHECK_NEAR(s.position.y, 0.0, 1e-9);
    CHECK_NEAR(s.position.z, 0.0, 1e-9);
    CHECK_NEAR(s.orbit.altitudeKm, 500.0, 1e-9);
    CHECK_NEAR(s.orbit.inclinationDeg, 0.0, 1e-9);
    CHECK_NEAR(s.orbit.raanDeg, 0.0, 1e-9);
    CHECK_NEAR(s.orbit.phaseDeg, 0.0, 1e-9);
    CHECK_EQ(s.orbit.band, std::string("LEO"));
    CHECK_NEAR(s.fuelFraction, 0.75, 1e-12);
    CHECK_NEAR(s.health.power, 1.0, 1e-12);
    CHECK(!s.activeEmergencyId.has_value());
}

ORCA_TEST(ViewProjection, VehicleViewMatchesKnownDeltaVAndStatus) {
    const SimulationEngine engine(buildTrivialScenario());
    const SimulationSnapshot snap = buildSnapshot(engine);
    CHECK_EQ(snap.rescueVehicles.size(), std::size_t{1});
    const VehicleView& v = snap.rescueVehicles[0];
    CHECK_EQ(v.id, std::string("RV-01"));
    CHECK_EQ(v.status, std::string("IDLE"));
    CHECK_NEAR(v.deltaVBudgetKms, 5.0, 1e-12);
    CHECK_NEAR(v.deltaVRemainingKms, 5.0, 1e-12);
    CHECK_NEAR(v.fuelFraction, 1.0, 1e-12);
    CHECK(!v.activeMissionId.has_value());
}

ORCA_TEST(ViewProjection, DebrisViewMatchesKnownFields) {
    const SimulationEngine engine(buildTrivialScenario());
    const SimulationSnapshot snap = buildSnapshot(engine);
    CHECK_EQ(snap.debris.size(), std::size_t{1});
    const DebrisView& d = snap.debris[0];
    CHECK_EQ(d.id, std::string("D-01"));
    CHECK_NEAR(d.hazardRadiusKm, 3.0, 1e-12);
    CHECK_NEAR(d.densityFactor, 0.4, 1e-12);
    CHECK(!d.detected);
}

ORCA_TEST(ViewProjection, EmptyStateProducesEmptyCollectionsNotErrors) {
    const SimulationEngine engine(buildTrivialScenario());
    const SimulationSnapshot snap = buildSnapshot(engine);
    CHECK(snap.emergencies.empty());
    CHECK(snap.missions.empty());
    CHECK(snap.recentEvents.empty());
    CHECK_EQ(snap.metrics.ticks, 0L);
}

ORCA_TEST(ViewProjection, MetricsViewMirrorsSimulationMetricsAfterRunning) {
    SimulationEngine engine(buildTrivialScenario());
    engine.start();
    engine.advance(10.0);
    const SimulationSnapshot snap = buildSnapshot(engine);
    CHECK_EQ(snap.metrics.ticks, 10L);
    CHECK_EQ(snap.simulationStatus, std::string("RUNNING"));
    CHECK_NEAR(snap.simulationTimeSec, 10.0, 1e-9);
}

// --- Demo scenario: mission/trajectory projection, cross-checked against Phase 8's own
// end-to-end demo test numbers (tests/simulation/end_to_end_demo_test.cpp), not re-derived.

ORCA_TEST(ViewProjection, MissionAndLegViewReflectARealTwoLegRoute) {
    SimulationEngine engine(buildDemoScenario());
    engine.start();
    scheduleScriptedFailures(engine.eventQueueMutable(), demoScenarioScriptedFailures());
    engine.advance(20.0);

    const SimulationSnapshot snap = buildSnapshot(engine);
    CHECK_EQ(snap.missions.size(), std::size_t{1});
    const MissionView& m = snap.missions[0];
    CHECK_EQ(m.state, std::string("IN_TRANSIT"));
    CHECK_EQ(m.vehicleId, std::string("RV-01"));
    CHECK_EQ(m.legs.size(), std::size_t{2});  // the same-period lock forces a 2-leg route

    // Leg 0 is current (mission just departed); leg 1 is upcoming. Every leg's endpoints
    // are real entity IDs from the graph, and arrival strictly follows departure.
    CHECK_EQ(m.legs[0].status, std::string("current"));
    CHECK_EQ(m.legs[1].status, std::string("upcoming"));
    CHECK(m.legs[0].arrivalTimeSec > m.legs[0].departureTimeSec);
    CHECK_NEAR(m.legs[1].departureTimeSec, m.legs[0].arrivalTimeSec, 1e-6);
    CHECK(!m.legs[0].fromEntityId.empty());
    CHECK(!m.legs[0].toEntityId.empty());
}

ORCA_TEST(ViewProjection, EmergencyViewReflectsUrgencyAndRemainingWindow) {
    SimulationEngine engine(buildDemoScenario());
    engine.start();
    scheduleScriptedFailures(engine.eventQueueMutable(), demoScenarioScriptedFailures());
    engine.advance(20.0);

    const SimulationSnapshot snap = buildSnapshot(engine);
    CHECK_EQ(snap.emergencies.size(), std::size_t{1});
    const EmergencyView& e = snap.emergencies[0];
    CHECK_EQ(e.subjectId, std::string("SAT-101"));
    CHECK_EQ(e.state, std::string("ASSIGNED"));
    CHECK_NEAR(e.detectedAtSec, 5.0, 1e-9);
    CHECK(e.remainingWindowSec < e.deadlineSec - e.detectedAtSec + 1e-6);
    CHECK(e.urgency >= 0.0 && e.urgency <= 1.0);
}

ORCA_TEST(ViewProjection, RecentEventsCaptureTheScriptedFailure) {
    SimulationEngine engine(buildDemoScenario());
    engine.start();
    scheduleScriptedFailures(engine.eventQueueMutable(), demoScenarioScriptedFailures());
    engine.advance(20.0);

    const SimulationSnapshot snap = buildSnapshot(engine);
    CHECK(!snap.recentEvents.empty());
    bool sawFailure = false;
    for (const EventView& e : snap.recentEvents) {
        if (e.kind == "SATELLITE_FAILURE") sawFailure = true;
    }
    CHECK(sawFailure);
}

ORCA_TEST(ViewProjection, SnapshotSerializesToValidCompactJson) {
    SimulationEngine engine(buildDemoScenario());
    engine.start();
    scheduleScriptedFailures(engine.eventQueueMutable(), demoScenarioScriptedFailures());
    engine.advance(20.0);

    const SimulationSnapshot snap = buildSnapshot(engine);
    const std::string dumped = toJson(snap).dump();
    CHECK(dumped.find('\n') == std::string::npos);
    const JsonValue reparsed = parseJson(dumped);
    CHECK_EQ(reparsed.find("type")->asString(), std::string("snapshot"));
    CHECK_EQ(reparsed.find("missions")->asArray().size(), std::size_t{1});
}

// --- Phase 10: graph view, Pareto front capture, replan explanation capture.

ORCA_TEST(ViewProjection, GraphViewEnumeratesEveryNodeAndEdge) {
    const SimulationEngine engine(buildDemoScenario());
    const SimulationSnapshot snap = buildSnapshot(engine);
    // The demo scenario has 4 graph nodes (SAT-101, MID-A, MID-B, RV-01) -- verified
    // directly, not re-derived from a formula.
    CHECK_EQ(snap.graph.nodes.size(), std::size_t{4});
    CHECK(!snap.graph.edges.empty());
    bool sawRv01 = false;
    for (const GraphNodeView& n : snap.graph.nodes) {
        CHECK(!n.id.empty());
        CHECK(!n.entityId.empty());
        CHECK(n.kind == "SATELLITE" || n.kind == "RESCUE_VEHICLE");
        if (n.entityId == "RV-01") {
            sawRv01 = true;
            CHECK_EQ(n.kind, std::string("RESCUE_VEHICLE"));
        }
    }
    CHECK(sawRv01);
    for (const GraphEdgeView& e : snap.graph.edges) {
        CHECK(!e.from.empty());
        CHECK(!e.to.empty());
        CHECK(e.cost.time >= 0.0);
        CHECK(e.cost.risk >= 0.0 && e.cost.risk <= 1.0);
    }
}

ORCA_TEST(ViewProjection, MissionCarriesItsOriginalAssignmentParetoFront) {
    SimulationEngine engine(buildDemoScenario());
    engine.start();
    scheduleScriptedFailures(engine.eventQueueMutable(), demoScenarioScriptedFailures());
    engine.advance(10.0);  // after t=5 dispatch, strictly before t=25 debris/replan

    const SimulationSnapshot snap = buildSnapshot(engine);
    CHECK_EQ(snap.missions.size(), std::size_t{1});
    const MissionView& m = snap.missions[0];
    CHECK(!m.paretoFront.empty());  // the demo scenario's two intermediates (MID-A/MID-B)
                                    // produce a genuine multi-point front, not a singleton

    bool sawRecommended = false;
    for (const ParetoPointView& p : m.paretoFront) {
        CHECK(!p.planId.empty());
        CHECK(p.cost.time >= 0.0);
        CHECK(p.cost.fuel >= 0.0);
        if (p.recommended) sawRecommended = true;
    }
    // Exactly one point matches the mission's actual adopted route -- the fresh PlanIds
    // minted for display purposes never collide with matching by route.
    CHECK(sawRecommended);
    CHECK(!m.explanation.has_value());  // no replan has happened yet -- honestly absent

    // Only pass 1 exists so far -- no replan means no second planning pass.
    CHECK_EQ(m.paretoHistory.size(), std::size_t{1});
    CHECK_EQ(m.paretoHistory[0].planningPass, 1);
}

ORCA_TEST(ViewProjection, MissionExplanationIsPopulatedOnlyAfterARealReplan) {
    SimulationEngine engine(buildDemoScenario());
    engine.start();
    scheduleScriptedFailures(engine.eventQueueMutable(), demoScenarioScriptedFailures());
    engine.scheduleEvent(demoScenarioDebrisAlertEvent());
    engine.advance(30.0);  // past t=25's debris alert -> a real local replan occurs

    const SimulationSnapshot snap = buildSnapshot(engine);
    CHECK_EQ(snap.missions.size(), std::size_t{1});
    const MissionView& m = snap.missions[0];
    CHECK_EQ(m.replanCount, 1);
    CHECK(m.explanation.has_value());
    CHECK(m.explanation->find("REPLAN") != std::string::npos);
    // Phase 11 brief §52-54's Pareto history fixes Phase 10's documented staleness
    // limitation: `paretoFront` now reflects the CURRENT (most recent) planning pass, so
    // after a real replan it is the single adopted plan, correctly marked recommended.
    CHECK_EQ(m.paretoFront.size(), std::size_t{1});
    CHECK(m.paretoFront[0].recommended);

    // Both passes are independently retained -- pass 1's original multi-point dispatch
    // front is still there, unmodified, alongside pass 2's single-point replan front.
    CHECK_EQ(m.paretoHistory.size(), std::size_t{2});
    CHECK_EQ(m.paretoHistory[0].planningPass, 1);
    CHECK(m.paretoHistory[0].points.size() >= std::size_t{1});
    CHECK_EQ(m.paretoHistory[1].planningPass, 2);
    CHECK_EQ(m.paretoHistory[1].points.size(), std::size_t{1});
    CHECK(m.paretoHistory[1].points[0].recommended);
}

// --- Phase 11: algorithm telemetry.

ORCA_TEST(ViewProjection, AlgorithmRunsCaptureRealComparisonMetricsAtDispatch) {
    SimulationEngine engine(buildDemoScenario());
    engine.start();
    scheduleScriptedFailures(engine.eventQueueMutable(), demoScenarioScriptedFailures());
    engine.advance(10.0);

    const SimulationSnapshot snap = buildSnapshot(engine);
    CHECK_EQ(snap.algorithmRuns.size(), std::size_t{4});  // Hungarian + Dijkstra + A* + Pareto

    bool sawHungarian = false, sawDijkstra = false, sawAStar = false, sawPareto = false;
    for (const AlgorithmRunView& r : snap.algorithmRuns) {
        CHECK(r.executionTimeMs >= 0.0);
        if (r.algorithm == "HUNGARIAN") {
            sawHungarian = true;
            CHECK(r.candidatePairsEvaluated.has_value());
            CHECK(!r.nodesExpanded.has_value());  // search-only field, correctly absent
        } else if (r.algorithm == "DIJKSTRA") {
            sawDijkstra = true;
            CHECK(r.nodesExpanded.has_value());
            CHECK(*r.nodesExpanded > 0);
            CHECK(!r.heuristicEvaluations.has_value());  // Dijkstra calls no heuristic
        } else if (r.algorithm == "ASTAR") {
            sawAStar = true;
            CHECK(r.nodesExpanded.has_value());
            CHECK(r.heuristicEvaluations.has_value());  // present (possibly 0) for A*
        } else if (r.algorithm == "PARETO") {
            sawPareto = true;
            CHECK(r.labelsGenerated.has_value());
            CHECK(*r.labelsGenerated > 0);
            CHECK(!r.nodesExpanded.has_value());  // Dijkstra/A*-only field, correctly absent
        }
    }
    CHECK(sawHungarian);
    CHECK(sawDijkstra);
    CHECK(sawAStar);
    CHECK(sawPareto);
}

ORCA_TEST(ViewProjection, AlgorithmRunHistoryIsBoundedAcrossManyDispatches) {
    // Advancing well past the demo's own single dispatch does not grow algorithmRuns
    // without bound -- SimulationState::kMaxAlgorithmRuns caps it.
    SimulationEngine engine(buildDemoScenario());
    engine.start();
    scheduleScriptedFailures(engine.eventQueueMutable(), demoScenarioScriptedFailures());
    engine.scheduleEvent(demoScenarioDebrisAlertEvent());
    engine.advance(600000.0);

    const SimulationSnapshot snap = buildSnapshot(engine);
    CHECK(snap.algorithmRuns.size() <= 20);
}
