/// Tests for `buildAssignmentCosts()` / `computeGlobalIdeal()` / `computeGlobalNadir()`
/// (`AssignmentCost.hpp`) — the corrected, global (not front-relative) normalisation that
/// makes assignment-matrix cells actually comparable across different (vehicle, emergency)
/// pairs (the file's central conceptual review, Phase 6 brief §9/§22).
///
/// Every numeric expectation below came from actually compiling and running the exact
/// scenario, not from hand arithmetic on the multi-objective score (which is not something
/// a human should reverse-engineer by inspection).

#include <cmath>

#include "../../cpp/optimization/AssignmentCost.hpp"
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
// The flagship conceptual test: two vehicles with very different fronts to the same
// emergency, showing front-relative scores collide while assignment cost correctly
// distinguishes them (AssignmentCost.hpp's file comment names this exact test by name).
// ---------------------------------------------------------------------------

ORCA_TEST(AssignmentCost, FrontRelativeScoresCollideButGlobalAssignmentCostDoesNot) {
    MissionGraph g;
    TransferModel model;
    const TransferConstraints c = permissive();

    // V1: a close, cheap neighbourhood, offering a genuine two-point front (fast-expensive
    // vs slow-cheap intermediate).
    const Orbit v1o = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, 0.0);
    const Orbit v1fast = Orbit::fromAltitude(560.0, degToRad(6.0), 0.0, degToRad(0.2));
    const Orbit v1cheap = Orbit::fromAltitude(520.0, degToRad(5.5), 0.0, degToRad(1.3));
    const Orbit e1o = Orbit::fromAltitude(600.0, degToRad(7.0), 0.0, degToRad(2.5));

    // V2: a far, expensive neighbourhood, same relative diamond shape but every absolute
    // cost far worse.
    const Orbit v2o = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, 0.0);
    const Orbit v2fast =
        Orbit::fromAltitude(2200.0, degToRad(45.0), degToRad(80.0), degToRad(0.2));
    const Orbit v2cheap =
        Orbit::fromAltitude(1800.0, degToRad(40.0), degToRad(70.0), degToRad(1.3));

    const NodeId v1n = g.addNode(GraphNode{NodeId{0}, VehicleId(1), v1o});
    const NodeId v2n = g.addNode(GraphNode{NodeId{0}, VehicleId(2), v2o});
    const NodeId e1n = g.addNode(GraphNode{NodeId{0}, SatelliteId(101), e1o});
    const NodeId v1fastN = g.addNode(GraphNode{NodeId{0}, SatelliteId(201), v1fast});
    const NodeId v1cheapN = g.addNode(GraphNode{NodeId{0}, SatelliteId(202), v1cheap});
    const NodeId v2fastN = g.addNode(GraphNode{NodeId{0}, SatelliteId(203), v2fast});
    const NodeId v2cheapN = g.addNode(GraphNode{NodeId{0}, SatelliteId(204), v2cheap});

    addEvaluatedEdge(g, model, c, v1n, v1fastN);
    addEvaluatedEdge(g, model, c, v1fastN, e1n);
    addEvaluatedEdge(g, model, c, v1n, v1cheapN);
    addEvaluatedEdge(g, model, c, v1cheapN, e1n);
    addEvaluatedEdge(g, model, c, v2n, v2fastN);
    addEvaluatedEdge(g, model, c, v2fastN, e1n);
    addEvaluatedEdge(g, model, c, v2n, v2cheapN);
    addEvaluatedEdge(g, model, c, v2cheapN, e1n);

    const RescueVehicle rv1(VehicleId(1), "RV-1", v1o, 10.0);
    const RescueVehicle rv2(VehicleId(2), "RV-2", v2o, 10.0);
    const Emergency e1(EmergencyId(101), SatelliteId(101), EmergencyKind::PowerFailure,
                       Severity::High, 0.0, 5.0e7);
    const std::vector<RescueVehicle> vehicles{rv1, rv2};
    const std::vector<Emergency> emergencies{e1};

    const auto fronts =
        generateAllCandidateFronts(g, model, vehicles, emergencies, 0.0, PlanId(1));
    const auto scored = scoreCandidates(fronts, OptimizationProfile::balanced());

    CHECK(fronts[0].feasible);
    CHECK(fronts[1].feasible);
    CHECK_EQ(fronts[0].front.solutions.size(), std::size_t{2});
    CHECK_EQ(fronts[1].front.solutions.size(), std::size_t{2});

    // The claim this test exists to verify: each vehicle's front-relative score (Phase 5's
    // MissionScorer, normalised against its OWN front) lands at the identical value despite
    // wildly different absolute mission quality (V1's fuel cost ~0.03 vs V2's ~0.86).
    CHECK_NEAR(scored[0].breakdown->totalScore, 0.3, 1e-6);
    CHECK_NEAR(scored[1].breakdown->totalScore, 0.3, 1e-6);
    CHECK_NEAR(scored[0].breakdown->totalScore, scored[1].breakdown->totalScore, 1e-9);

    // The corrected, GLOBAL assignment cost must NOT collide the same way -- V2 is
    // genuinely worse and must cost strictly more.
    const auto costs =
        buildAssignmentCosts(fronts, scored, emergencies, 0.0, OptimizationProfile::balanced());
    CHECK_NEAR(costs[0].cost, 0.45, 1e-6);
    CHECK_NEAR(costs[1].cost, 1.05, 1e-6);
    CHECK(costs[1].cost > costs[0].cost);
}

// ---------------------------------------------------------------------------
// Global bounds span the whole matrix, not one pair (5).
// ---------------------------------------------------------------------------

ORCA_TEST(AssignmentCost, GlobalIdealAndNadirAreComponentwiseAcrossTheWholeMatrix) {
    MissionGraph g;
    TransferModel model;
    const TransferConstraints c = permissive();

    const Orbit v1o = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, 0.0);
    const Orbit v2o = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, 0.0);
    const Orbit e1o = Orbit::fromAltitude(550.0, degToRad(6.0), degToRad(5.0), degToRad(0.1));
    const Orbit e2o = Orbit::fromAltitude(1600.0, degToRad(40.0), degToRad(90.0), degToRad(0.6));

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

    const ObjectiveVector ideal = computeGlobalIdeal(fronts, scored);
    const ObjectiveVector nadir = computeGlobalNadir(fronts, scored);

    // Every feasible candidate's own selected cost must lie within [ideal, nadir]
    // componentwise -- that is the defining property of a componentwise min/max over the
    // full set, checked directly rather than trusted.
    for (std::size_t i = 0; i < fronts.size(); ++i) {
        if (!fronts[i].feasible) continue;
        const ObjectiveVector& cost = fronts[i].front.solutions[*scored[i].selectedPlanIndex].totalCost();
        CHECK(cost.time >= ideal.time - 1e-6 && cost.time <= nadir.time + 1e-6);
        CHECK(cost.fuel >= ideal.fuel - 1e-6 && cost.fuel <= nadir.fuel + 1e-6);
        CHECK(cost.risk >= ideal.risk - 1e-6 && cost.risk <= nadir.risk + 1e-6);
        CHECK(cost.distance >= ideal.distance - 1e-6 && cost.distance <= nadir.distance + 1e-6);
    }
    // And at least one candidate must actually achieve each bound (componentwise min/max
    // are tight, not merely permissive).
    bool someAchievesIdealTime = false;
    bool someAchievesNadirFuel = false;
    for (std::size_t i = 0; i < fronts.size(); ++i) {
        if (!fronts[i].feasible) continue;
        const ObjectiveVector& cost = fronts[i].front.solutions[*scored[i].selectedPlanIndex].totalCost();
        if (std::fabs(cost.time - ideal.time) < 1e-6) someAchievesIdealTime = true;
        if (std::fabs(cost.fuel - nadir.fuel) < 1e-6) someAchievesNadirFuel = true;
    }
    CHECK(someAchievesIdealTime);
    CHECK(someAchievesNadirFuel);
}

// ---------------------------------------------------------------------------
// Infeasible candidates are excluded from global bounds, and get the sentinel cost (4).
// ---------------------------------------------------------------------------

ORCA_TEST(AssignmentCost, InfeasibleCandidateGetsTheDocumentedSentinelCost) {
    MissionGraph g;
    TransferModel model;
    const TransferConstraints c = permissive();

    const Orbit v1o = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, 0.0);
    const Orbit e1o = Orbit::fromAltitude(550.0, degToRad(6.0), degToRad(5.0), degToRad(0.1));
    const NodeId v1n = g.addNode(GraphNode{NodeId{0}, VehicleId(1), v1o});
    const NodeId e1n = g.addNode(GraphNode{NodeId{0}, SatelliteId(101), e1o});
    addEvaluatedEdge(g, model, c, v1n, e1n);

    // Vehicle 2 has a starvation-level delta-v budget -- its only edge is infeasible under
    // its own real (tiny) budget, which candidate generation enforces via
    // vehicle.deltaVRemainingKms(), not the permissive graph-build constraints above.
    const NodeId v2n = g.addNode(GraphNode{NodeId{0}, VehicleId(2), v1o});
    addEvaluatedEdge(g, model, c, v2n, e1n);

    const RescueVehicle rv1(VehicleId(1), "RV-1", v1o, 5.0);
    const RescueVehicle rv2(VehicleId(2), "RV-2", v1o, 0.00001);
    const Emergency e1(EmergencyId(101), SatelliteId(101), EmergencyKind::PowerFailure,
                       Severity::High, 0.0, 5.0e7);
    const std::vector<RescueVehicle> vehicles{rv1, rv2};
    const std::vector<Emergency> emergencies{e1};

    const auto fronts =
        generateAllCandidateFronts(g, model, vehicles, emergencies, 0.0, PlanId(1));
    const auto scored = scoreCandidates(fronts, OptimizationProfile::balanced());
    CHECK(fronts[0].feasible);
    CHECK(!fronts[1].feasible);

    const auto costs =
        buildAssignmentCosts(fronts, scored, emergencies, 0.0, OptimizationProfile::balanced());
    CHECK(costs[0].feasible);
    CHECK(costs[0].cost < kInfeasibleAssignmentCost);
    CHECK(!costs[1].feasible);
    CHECK_NEAR(costs[1].cost, kInfeasibleAssignmentCost, 1.0);
}

// ---------------------------------------------------------------------------
// Priority is computed once per emergency, identical across every vehicle's entry (4).
// ---------------------------------------------------------------------------

ORCA_TEST(AssignmentCost, PriorityIsIdenticalAcrossEveryVehicleForTheSameEmergency) {
    MissionGraph g;
    TransferModel model;
    const TransferConstraints c = permissive();

    const Orbit v1o = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, 0.0);
    const Orbit v2o = Orbit::fromAltitude(700.0, degToRad(10.0), degToRad(20.0), degToRad(0.4));
    const Orbit e1o = Orbit::fromAltitude(550.0, degToRad(6.0), degToRad(5.0), degToRad(0.1));
    const NodeId v1n = g.addNode(GraphNode{NodeId{0}, VehicleId(1), v1o});
    const NodeId v2n = g.addNode(GraphNode{NodeId{0}, VehicleId(2), v2o});
    const NodeId e1n = g.addNode(GraphNode{NodeId{0}, SatelliteId(101), e1o});
    addEvaluatedEdge(g, model, c, v1n, e1n);
    addEvaluatedEdge(g, model, c, v2n, e1n);

    const RescueVehicle rv1(VehicleId(1), "RV-1", v1o, 10.0);
    const RescueVehicle rv2(VehicleId(2), "RV-2", v2o, 10.0);
    const Emergency e1(EmergencyId(101), SatelliteId(101), EmergencyKind::FuelCritical,
                       Severity::Critical, 0.0, 5.0e7);
    const std::vector<RescueVehicle> vehicles{rv1, rv2};
    const std::vector<Emergency> emergencies{e1};

    const auto fronts =
        generateAllCandidateFronts(g, model, vehicles, emergencies, 0.0, PlanId(1));
    const auto scored = scoreCandidates(fronts, OptimizationProfile::balanced());
    const auto costs =
        buildAssignmentCosts(fronts, scored, emergencies, 0.0, OptimizationProfile::balanced());

    CHECK_NEAR(costs[0].priority, costs[1].priority, 1e-12);
}

ORCA_TEST(AssignmentCost, RejectsEmergenciesListThatDoesNotCoverScoredCandidates) {
    // `scored` references an emergency that the separately-passed `emergencies` list omits
    // -- must fail with this codebase's uniform caller-error exception
    // (std::invalid_argument via requireThat), not an unrelated std::out_of_range from an
    // internal map lookup (found during Phase 6's adversarial self-review).
    MissionGraph g;
    TransferModel model;
    const TransferConstraints c = permissive();
    const Orbit v1o = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, 0.0);
    const Orbit e1o = Orbit::fromAltitude(550.0, degToRad(6.0), degToRad(5.0), degToRad(0.1));
    const NodeId v1n = g.addNode(GraphNode{NodeId{0}, VehicleId(1), v1o});
    const NodeId e1n = g.addNode(GraphNode{NodeId{0}, SatelliteId(101), e1o});
    addEvaluatedEdge(g, model, c, v1n, e1n);

    const RescueVehicle rv1(VehicleId(1), "RV-1", v1o, 10.0);
    const Emergency e1(EmergencyId(101), SatelliteId(101), EmergencyKind::PowerFailure,
                       Severity::Low, 0.0, 5.0e7);
    const std::vector<RescueVehicle> vehicles{rv1};
    const std::vector<Emergency> emergencies{e1};

    const auto fronts =
        generateAllCandidateFronts(g, model, vehicles, emergencies, 0.0, PlanId(1));
    const auto scored = scoreCandidates(fronts, OptimizationProfile::balanced());

    const std::vector<Emergency> emptyEmergencies;
    CHECK_THROWS(
        buildAssignmentCosts(fronts, scored, emptyEmergencies, 0.0, OptimizationProfile::balanced()));
}

ORCA_TEST(AssignmentCost, FeasibleCostsAreBoundedInZeroToTwo) {
    // (2 - priority) in [1,2] and the score component in [0,1] -- documented bound
    // (AssignmentCost.hpp's file comment); checked against real search output, not just
    // the formula in isolation.
    MissionGraph g;
    TransferModel model;
    const TransferConstraints c = permissive();
    const Orbit v1o = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, 0.0);
    const Orbit e1o = Orbit::fromAltitude(900.0, degToRad(20.0), degToRad(40.0), degToRad(1.0));
    const NodeId v1n = g.addNode(GraphNode{NodeId{0}, VehicleId(1), v1o});
    const NodeId e1n = g.addNode(GraphNode{NodeId{0}, SatelliteId(101), e1o});
    addEvaluatedEdge(g, model, c, v1n, e1n);

    const RescueVehicle rv1(VehicleId(1), "RV-1", v1o, 10.0);
    const Emergency e1(EmergencyId(101), SatelliteId(101), EmergencyKind::PowerFailure,
                       Severity::Low, 0.0, 5.0e7);
    const std::vector<RescueVehicle> vehicles{rv1};
    const std::vector<Emergency> emergencies{e1};

    const auto fronts =
        generateAllCandidateFronts(g, model, vehicles, emergencies, 0.0, PlanId(1));
    const auto scored = scoreCandidates(fronts, OptimizationProfile::balanced());
    const auto costs =
        buildAssignmentCosts(fronts, scored, emergencies, 0.0, OptimizationProfile::balanced());

    CHECK(costs[0].feasible);
    CHECK(costs[0].cost >= 0.0 && costs[0].cost <= 2.0);
}
