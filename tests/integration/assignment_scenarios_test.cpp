/// Multi-emergency integration scenarios A-G (Phase 6 brief §19) — the full pipeline
/// (candidate generation over the real Pareto search -> scoring -> global assignment cost
/// -> padded matrix -> Hungarian) exercised end to end against real, if small, orbital
/// scenarios. Every number below came from actually compiling and running the exact
/// scenario; none are hand-derived.
///
/// Scenario F (real-data greedy-vs-Hungarian counterexample) needs both solvers and is kept
/// in its own test below, alongside A-E and G.

#include "../../cpp/optimization/AssignmentCost.hpp"
#include "../../cpp/optimization/AssignmentMatrix.hpp"
#include "../../cpp/optimization/AssignmentResult.hpp"
#include "../../cpp/optimization/CandidateGeneration.hpp"
#include "../../cpp/domain/RescueVehicle.hpp"
#include "../../cpp/domain/Emergency.hpp"
#include "../../cpp/graph/MissionGraph.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {

TransferConstraints permissive() {
    TransferConstraints c;
    c.maxDeltaVKms = 1.0e6;
    c.deltaVBudgetKms = 1.0e6;
    c.availableFuelFraction = 1.0;
    c.maxWaitSec = 1.0e13;
    c.latestArrivalSec = 1.0e18;
    return c;
}

void addEvaluatedEdge(MissionGraph& g, const TransferModel& model, const TransferConstraints& c,
                      NodeId u, NodeId v) {
    g.addEdge(u, v, model.evaluate(g.node(u).orbit(), g.node(v).orbit(), 0.0, c), 0.0);
}

}  // namespace

// ---------------------------------------------------------------------------
// Scenario A: independent, easy -- no competition, the obvious pairing is optimal.
// ---------------------------------------------------------------------------

ORCA_TEST(AssignmentScenarios, ScenarioA_IndependentEasy) {
    MissionGraph g;
    TransferModel model;
    const TransferConstraints c = permissive();

    const Orbit v1o = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, 0.0);
    const Orbit e1o = Orbit::fromAltitude(520.0, degToRad(6.0), degToRad(2.0), degToRad(0.1));
    const Orbit v2o =
        Orbit::fromAltitude(2000.0, degToRad(50.0), degToRad(100.0), degToRad(1.0));
    const Orbit e2o =
        Orbit::fromAltitude(2020.0, degToRad(51.0), degToRad(101.0), degToRad(1.05));

    const NodeId v1n = g.addNode(GraphNode{NodeId{0}, VehicleId(1), v1o});
    const NodeId v2n = g.addNode(GraphNode{NodeId{0}, VehicleId(2), v2o});
    const NodeId e1n = g.addNode(GraphNode{NodeId{0}, SatelliteId(101), e1o});
    const NodeId e2n = g.addNode(GraphNode{NodeId{0}, SatelliteId(102), e2o});
    addEvaluatedEdge(g, model, c, v1n, e1n);
    addEvaluatedEdge(g, model, c, v1n, e2n);
    addEvaluatedEdge(g, model, c, v2n, e1n);
    addEvaluatedEdge(g, model, c, v2n, e2n);

    const RescueVehicle rv1(VehicleId(1), "RV-1", v1o, 10.0);
    const RescueVehicle rv2(VehicleId(2), "RV-2", v2o, 10.0);
    const Emergency e1(EmergencyId(101), SatelliteId(101), EmergencyKind::PowerFailure,
                       Severity::High, 0.0, 5.0e7);
    const Emergency e2(EmergencyId(102), SatelliteId(102), EmergencyKind::CommsLoss,
                       Severity::Moderate, 0.0, 5.0e7);
    const std::vector<RescueVehicle> vehicles{rv1, rv2};
    const std::vector<Emergency> emergencies{e1, e2};

    const auto fronts =
        generateAllCandidateFronts(g, model, vehicles, emergencies, 0.0, PlanId(1));
    const auto scored = scoreCandidates(fronts, OptimizationProfile::balanced());
    const auto costs =
        buildAssignmentCosts(fronts, scored, emergencies, 0.0, OptimizationProfile::balanced());
    const std::vector<VehicleId> vids{rv1.id(), rv2.id()};
    const std::vector<EmergencyId> eids{e1.id(), e2.id()};
    const auto matrix = buildAssignmentMatrix(costs, vids, eids);
    const auto result = solveAssignmentHungarian(matrix, fronts, scored);

    CHECK(result.status == AssignmentStatus::Success);
    CHECK_EQ(result.assignments.size(), std::size_t{2});
    CHECK_NEAR(result.totalCost, 0.9198, 1e-3);
    for (const auto& a : result.assignments) {
        if (a.vehicle.value() == 1) {
            CHECK_EQ(a.emergency.value(), std::uint32_t{101});
        } else {
            CHECK_EQ(a.emergency.value(), std::uint32_t{102});
        }
    }
}

// ---------------------------------------------------------------------------
// Scenario B: competition -- one vehicle is better at everything, but the jointly optimal
// split still requires it to give up its own best match to the other vehicle.
// ---------------------------------------------------------------------------

ORCA_TEST(AssignmentScenarios, ScenarioB_Competition) {
    MissionGraph g;
    TransferModel model;
    const TransferConstraints c = permissive();

    const Orbit v1o = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, 0.0);
    const Orbit v2o =
        Orbit::fromAltitude(700.0, degToRad(15.0), degToRad(10.0), degToRad(0.3));
    const Orbit e1o = Orbit::fromAltitude(520.0, degToRad(6.0), degToRad(2.0), degToRad(0.1));
    const Orbit e2o = Orbit::fromAltitude(560.0, degToRad(7.0), degToRad(3.0), degToRad(0.2));

    const NodeId v1n = g.addNode(GraphNode{NodeId{0}, VehicleId(1), v1o});
    const NodeId v2n = g.addNode(GraphNode{NodeId{0}, VehicleId(2), v2o});
    const NodeId e1n = g.addNode(GraphNode{NodeId{0}, SatelliteId(101), e1o});
    const NodeId e2n = g.addNode(GraphNode{NodeId{0}, SatelliteId(102), e2o});
    addEvaluatedEdge(g, model, c, v1n, e1n);
    addEvaluatedEdge(g, model, c, v1n, e2n);
    addEvaluatedEdge(g, model, c, v2n, e1n);
    addEvaluatedEdge(g, model, c, v2n, e2n);

    const RescueVehicle rv1(VehicleId(1), "RV-1", v1o, 5.0);
    const RescueVehicle rv2(VehicleId(2), "RV-2", v2o, 5.0);
    const Emergency e1(EmergencyId(101), SatelliteId(101), EmergencyKind::PowerFailure,
                       Severity::High, 0.0, 5.0e7);
    const Emergency e2(EmergencyId(102), SatelliteId(102), EmergencyKind::CommsLoss,
                       Severity::Moderate, 0.0, 5.0e7);
    const std::vector<RescueVehicle> vehicles{rv1, rv2};
    const std::vector<Emergency> emergencies{e1, e2};

    const auto fronts =
        generateAllCandidateFronts(g, model, vehicles, emergencies, 0.0, PlanId(1));
    const auto scored = scoreCandidates(fronts, OptimizationProfile::balanced());
    const auto costs =
        buildAssignmentCosts(fronts, scored, emergencies, 0.0, OptimizationProfile::balanced());
    const std::vector<VehicleId> vids{rv1.id(), rv2.id()};
    const std::vector<EmergencyId> eids{e1.id(), e2.id()};
    const auto matrix = buildAssignmentMatrix(costs, vids, eids);
    const auto result = solveAssignmentHungarian(matrix, fronts, scored);

    // V1 is cheaper at BOTH emergencies (0.45 vs 1.02 for E1, 0.2284 vs 1.0258 for E2), but
    // the optimal split (V1->E2, V2->E1 = 1.2532) beats letting V1 take its own best match
    // (V1->E1, V2->E2 = 1.4758) -- the arbitration this scenario exists to check.
    CHECK(result.status == AssignmentStatus::Success);
    CHECK_NEAR(result.totalCost, 1.2532, 1e-3);
    for (const auto& a : result.assignments) {
        if (a.vehicle.value() == 1) {
            CHECK_EQ(a.emergency.value(), std::uint32_t{102});
        } else {
            CHECK_EQ(a.emergency.value(), std::uint32_t{101});
        }
    }
}

// ---------------------------------------------------------------------------
// Scenario C: capability/reachability restriction -- one vehicle's delta-v budget lets it
// reach the near emergency but not the far one; the assignment must route around that,
// never selecting the infeasible pair.
// ---------------------------------------------------------------------------

ORCA_TEST(AssignmentScenarios, ScenarioC_CapabilityRestriction) {
    MissionGraph g;
    TransferModel model;
    const TransferConstraints c = permissive();

    const Orbit v1o = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, 0.0);
    const Orbit v2o = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, 0.0);
    const Orbit e1o = Orbit::fromAltitude(520.0, degToRad(6.0), degToRad(2.0), degToRad(0.1));
    const Orbit e2o = Orbit::fromAltitude(540.0, degToRad(7.0), degToRad(3.0), degToRad(0.2));

    const NodeId v1n = g.addNode(GraphNode{NodeId{0}, VehicleId(1), v1o});
    const NodeId v2n = g.addNode(GraphNode{NodeId{0}, VehicleId(2), v2o});
    const NodeId e1n = g.addNode(GraphNode{NodeId{0}, SatelliteId(101), e1o});
    const NodeId e2n = g.addNode(GraphNode{NodeId{0}, SatelliteId(102), e2o});
    addEvaluatedEdge(g, model, c, v1n, e1n);
    addEvaluatedEdge(g, model, c, v1n, e2n);
    addEvaluatedEdge(g, model, c, v2n, e1n);
    addEvaluatedEdge(g, model, c, v2n, e2n);

    const RescueVehicle rv1(VehicleId(1), "RV-1", v1o, 5.0);
    // Deliberately starved: reaches E1 (near) but not E2 (far), per real search output.
    const RescueVehicle rv2(VehicleId(2), "RV-2", v2o, 0.2);
    const Emergency e1(EmergencyId(101), SatelliteId(101), EmergencyKind::PowerFailure,
                       Severity::High, 0.0, 5.0e7);
    const Emergency e2(EmergencyId(102), SatelliteId(102), EmergencyKind::CommsLoss,
                       Severity::Moderate, 0.0, 5.0e7);
    const std::vector<RescueVehicle> vehicles{rv1, rv2};
    const std::vector<Emergency> emergencies{e1, e2};

    const auto fronts =
        generateAllCandidateFronts(g, model, vehicles, emergencies, 0.0, PlanId(1));
    CHECK(fronts[0].feasible);   // V1, E1
    CHECK(fronts[1].feasible);   // V1, E2
    CHECK(fronts[2].feasible);   // V2, E1
    CHECK(!fronts[3].feasible);  // V2, E2 -- reachability restriction
    CHECK(fronts[3].infeasibilityReason.has_value());

    const auto scored = scoreCandidates(fronts, OptimizationProfile::balanced());
    const auto costs =
        buildAssignmentCosts(fronts, scored, emergencies, 0.0, OptimizationProfile::balanced());
    const std::vector<VehicleId> vids{rv1.id(), rv2.id()};
    const std::vector<EmergencyId> eids{e1.id(), e2.id()};
    const auto matrix = buildAssignmentMatrix(costs, vids, eids);
    const auto result = solveAssignmentHungarian(matrix, fronts, scored);

    CHECK(result.status == AssignmentStatus::Success);
    CHECK_NEAR(result.totalCost, 1.5499, 1e-3);
    for (const auto& a : result.assignments) {
        // V2 can only ever legally serve E1 -- never the infeasible (V2, E2) pair.
        if (a.vehicle.value() == 2) {
            CHECK_EQ(a.emergency.value(), std::uint32_t{101});
        }
    }
}

// ---------------------------------------------------------------------------
// Scenario D: deadline restriction -- infeasibility from a survival window, not delta-v.
// ---------------------------------------------------------------------------

ORCA_TEST(AssignmentScenarios, ScenarioD_DeadlineRestriction) {
    MissionGraph g;
    TransferModel model;
    const TransferConstraints c = permissive();

    const Orbit v1o = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, 0.0);
    const Orbit v2o =
        Orbit::fromAltitude(1400.0, degToRad(30.0), degToRad(60.0), degToRad(0.5));
    const Orbit e1o = Orbit::fromAltitude(520.0, degToRad(6.0), degToRad(2.0), degToRad(0.1));

    const NodeId v1n = g.addNode(GraphNode{NodeId{0}, VehicleId(1), v1o});
    const NodeId v2n = g.addNode(GraphNode{NodeId{0}, VehicleId(2), v2o});
    const NodeId e1n = g.addNode(GraphNode{NodeId{0}, SatelliteId(101), e1o});
    addEvaluatedEdge(g, model, c, v1n, e1n);
    addEvaluatedEdge(g, model, c, v2n, e1n);

    const RescueVehicle rv1(VehicleId(1), "RV-1", v1o, 5.0);
    const RescueVehicle rv2(VehicleId(2), "RV-2", v2o, 5.0);
    // A 50000s window: infeasible for one vehicle's real geometry/phasing (verified by
    // running the actual search, not assumed from altitude alone -- deadline infeasibility
    // depends on phase alignment as much as distance), feasible for the other's.
    const Emergency e1(EmergencyId(101), SatelliteId(101), EmergencyKind::PowerFailure,
                       Severity::High, 0.0, 50000.0);
    const std::vector<RescueVehicle> vehicles{rv1, rv2};
    const std::vector<Emergency> emergencies{e1};

    const auto fronts =
        generateAllCandidateFronts(g, model, vehicles, emergencies, 0.0, PlanId(1));
    CHECK(!fronts[0].feasible);  // V1, E1 -- infeasible under this tight deadline
    CHECK(fronts[0].infeasibilityReason == ConstraintKind::SurvivalWindow);
    CHECK(fronts[1].feasible);   // V2, E1 -- feasible

    const auto scored = scoreCandidates(fronts, OptimizationProfile::balanced());
    const auto costs =
        buildAssignmentCosts(fronts, scored, emergencies, 0.0, OptimizationProfile::balanced());
    const std::vector<VehicleId> vids{rv1.id(), rv2.id()};
    const std::vector<EmergencyId> eids{e1.id()};
    const auto matrix = buildAssignmentMatrix(costs, vids, eids);
    const auto result = solveAssignmentHungarian(matrix, fronts, scored);

    CHECK(result.status == AssignmentStatus::Success);
    CHECK_EQ(result.assignments.size(), std::size_t{1});
    CHECK_EQ(result.assignments[0].vehicle.value(), std::uint32_t{2});
    CHECK_EQ(result.assignments[0].emergency.value(), std::uint32_t{101});
}

// ---------------------------------------------------------------------------
// Scenario E: priority under scarcity -- one vehicle, two emergencies; the higher-priority
// one (Critical severity, imminent deadline) must be the one served, not the cheaper but
// lower-priority one. This is also the regression test for the priority-penalty direction
// bug found while building this scenario (AssignmentMatrix.cpp's unassignedPenalty()).
// ---------------------------------------------------------------------------

ORCA_TEST(AssignmentScenarios, ScenarioE_PriorityUnderScarcity) {
    MissionGraph g;
    TransferModel model;
    const TransferConstraints c = permissive();

    const Orbit v1o = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, 0.0);
    const Orbit e1o = Orbit::fromAltitude(550.0, degToRad(6.0), degToRad(3.0), degToRad(0.2));
    const Orbit e2o = Orbit::fromAltitude(555.0, degToRad(6.0), degToRad(3.0), degToRad(0.25));

    const NodeId v1n = g.addNode(GraphNode{NodeId{0}, VehicleId(1), v1o});
    const NodeId e1n = g.addNode(GraphNode{NodeId{0}, SatelliteId(101), e1o});
    const NodeId e2n = g.addNode(GraphNode{NodeId{0}, SatelliteId(102), e2o});
    addEvaluatedEdge(g, model, c, v1n, e1n);
    addEvaluatedEdge(g, model, c, v1n, e2n);

    const RescueVehicle rv1(VehicleId(1), "RV-1", v1o, 5.0);
    // E1: Low severity, effectively no urgency yet (nowSec far from deadline).
    const Emergency e1(EmergencyId(101), SatelliteId(101), EmergencyKind::PowerFailure,
                       Severity::Low, 0.0, 5.0e7);
    // E2: Critical severity -- higher priority even though its own route costs a little
    // more than E1's.
    const Emergency e2(EmergencyId(102), SatelliteId(102), EmergencyKind::FuelCritical,
                       Severity::Critical, 0.0, 5.0e7);
    const std::vector<RescueVehicle> vehicles{rv1};
    const std::vector<Emergency> emergencies{e1, e2};

    const auto fronts =
        generateAllCandidateFronts(g, model, vehicles, emergencies, 0.0, PlanId(1));
    const auto scored = scoreCandidates(fronts, OptimizationProfile::balanced());
    const auto costs =
        buildAssignmentCosts(fronts, scored, emergencies, 0.0, OptimizationProfile::balanced());
    CHECK(costs[1].priority > costs[0].priority);  // E2 outranks E1

    const std::vector<VehicleId> vids{rv1.id()};
    const std::vector<EmergencyId> eids{e1.id(), e2.id()};
    const auto matrix = buildAssignmentMatrix(costs, vids, eids);
    const auto result = solveAssignmentHungarian(matrix, fronts, scored);

    CHECK(result.status == AssignmentStatus::PartiallyAssignable);
    CHECK_EQ(result.assignments.size(), std::size_t{1});
    // The sole vehicle must serve the higher-priority emergency (E2), not the cheaper but
    // lower-priority one (E1) -- the direction the priority-penalty bug fix corrected.
    CHECK_EQ(result.assignments[0].emergency.value(), std::uint32_t{102});
    CHECK_EQ(result.unassignedEmergencyCount, 1);
}

// ---------------------------------------------------------------------------
// Scenario F: greedy failure with real ORCA-derived data -- both solvers reach full
// coverage (an apples-to-apples comparison), but Hungarian strictly beats greedy purely
// because two vehicles share the exact same orbit and differ only in remaining delta-v
// budget (Phase 6 brief §16, §19F, §28).
// ---------------------------------------------------------------------------

ORCA_TEST(AssignmentScenarios, ScenarioF_GreedyFailureWithRealData) {
    MissionGraph g;
    TransferModel model;
    const TransferConstraints c = permissive();

    const Orbit v1o = Orbit::fromAltitude(500.0, degToRad(10.0), 0.0, 0.0);
    const Orbit v2o = Orbit::fromAltitude(500.0, degToRad(10.0), 0.0, 0.0);
    const Orbit e1o =
        Orbit::fromAltitude(550.0, degToRad(12.0), degToRad(10.0), degToRad(0.1));
    const Orbit e2o = Orbit::fromAltitude(900.0, degToRad(30.0), degToRad(60.0), degToRad(0.4));

    const NodeId v1n = g.addNode(GraphNode{NodeId{0}, VehicleId(1), v1o});
    const NodeId v2n = g.addNode(GraphNode{NodeId{0}, VehicleId(2), v2o});
    const NodeId e1n = g.addNode(GraphNode{NodeId{0}, SatelliteId(101), e1o});
    const NodeId e2n = g.addNode(GraphNode{NodeId{0}, SatelliteId(102), e2o});
    addEvaluatedEdge(g, model, c, v1n, e1n);
    addEvaluatedEdge(g, model, c, v1n, e2n);
    addEvaluatedEdge(g, model, c, v2n, e1n);
    addEvaluatedEdge(g, model, c, v2n, e2n);

    // Same orbit -- the only difference is remaining delta-v budget.
    const RescueVehicle rv1(VehicleId(1), "RV-1", v1o, 5.0);
    const RescueVehicle rv2(VehicleId(2), "RV-2", v2o, 2.8);
    const Emergency e1(EmergencyId(101), SatelliteId(101), EmergencyKind::PowerFailure,
                       Severity::High, 0.0, 5.0e6);
    const Emergency e2(EmergencyId(102), SatelliteId(102), EmergencyKind::CommsLoss,
                       Severity::Moderate, 0.0, 5.0e6);
    const std::vector<RescueVehicle> vehicles{rv1, rv2};
    const std::vector<Emergency> emergencies{e1, e2};

    const auto fronts =
        generateAllCandidateFronts(g, model, vehicles, emergencies, 0.0, PlanId(1));
    const auto scored = scoreCandidates(fronts, OptimizationProfile::balanced());
    const auto costs =
        buildAssignmentCosts(fronts, scored, emergencies, 0.0, OptimizationProfile::balanced());
    const std::vector<VehicleId> vids{rv1.id(), rv2.id()};
    const std::vector<EmergencyId> eids{e1.id(), e2.id()};
    const auto matrix = buildAssignmentMatrix(costs, vids, eids);

    const auto hungarianResult = solveAssignmentHungarian(matrix, fronts, scored);
    const auto greedyResult = solveAssignmentGreedy(matrix, fronts, scored);

    CHECK(hungarianResult.status == AssignmentStatus::Success);
    CHECK(greedyResult.status == AssignmentStatus::Success);
    CHECK_NEAR(hungarianResult.totalCost, 1.167632, 1e-4);
    CHECK_NEAR(greedyResult.totalCost, 1.570000, 1e-4);
    CHECK(hungarianResult.totalCost < greedyResult.totalCost);

    // Greedy grabs the globally cheapest cell (V1->E1) first, stranding the fuel-limited
    // V2 with the expensive job; Hungarian looks ahead and swaps.
    CHECK_EQ(greedyResult.assignments[0].vehicle.value(), std::uint32_t{1});
    CHECK_EQ(greedyResult.assignments[0].emergency.value(), std::uint32_t{101});
    bool hungarianSwapsV1ToE2 = false;
    for (const auto& a : hungarianResult.assignments) {
        if (a.vehicle.value() == 1 && a.emergency.value() == 102) hungarianSwapsV1ToE2 = true;
    }
    CHECK(hungarianSwapsV1ToE2);
}

// ---------------------------------------------------------------------------
// Scenario G: no feasible global solution -- every vehicle is starved of delta-v; the
// pipeline must honestly report INFEASIBLE, not fabricate an assignment.
// ---------------------------------------------------------------------------

ORCA_TEST(AssignmentScenarios, ScenarioG_NoFeasibleGlobalSolution) {
    MissionGraph g;
    TransferModel model;
    const TransferConstraints c = permissive();

    const Orbit v1o = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, 0.0);
    const Orbit v2o = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, 0.0);
    const Orbit e1o =
        Orbit::fromAltitude(3000.0, degToRad(80.0), degToRad(150.0), degToRad(2.0));

    const NodeId v1n = g.addNode(GraphNode{NodeId{0}, VehicleId(1), v1o});
    const NodeId v2n = g.addNode(GraphNode{NodeId{0}, VehicleId(2), v2o});
    const NodeId e1n = g.addNode(GraphNode{NodeId{0}, SatelliteId(101), e1o});
    addEvaluatedEdge(g, model, c, v1n, e1n);
    addEvaluatedEdge(g, model, c, v2n, e1n);

    const RescueVehicle rv1(VehicleId(1), "RV-1", v1o, 0.001);
    const RescueVehicle rv2(VehicleId(2), "RV-2", v2o, 0.001);
    const Emergency e1(EmergencyId(101), SatelliteId(101), EmergencyKind::PowerFailure,
                       Severity::High, 0.0, 5.0e7);
    const std::vector<RescueVehicle> vehicles{rv1, rv2};
    const std::vector<Emergency> emergencies{e1};

    const auto fronts =
        generateAllCandidateFronts(g, model, vehicles, emergencies, 0.0, PlanId(1));
    CHECK(!fronts[0].feasible);
    CHECK(!fronts[1].feasible);

    const auto scored = scoreCandidates(fronts, OptimizationProfile::balanced());
    const auto costs =
        buildAssignmentCosts(fronts, scored, emergencies, 0.0, OptimizationProfile::balanced());
    const std::vector<VehicleId> vids{rv1.id(), rv2.id()};
    const std::vector<EmergencyId> eids{e1.id()};
    const auto matrix = buildAssignmentMatrix(costs, vids, eids);
    const auto result = solveAssignmentHungarian(matrix, fronts, scored);

    CHECK(result.status == AssignmentStatus::Infeasible);
    CHECK(result.assignments.empty());
    CHECK_NEAR(result.totalCost, 0.0, 1e-9);
    CHECK_EQ(result.unassignedEmergencyCount, 1);
    CHECK_EQ(result.unusedVehicleCount, 2);
}
