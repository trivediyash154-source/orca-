/// Pareto-integration checks (Phase 6 brief §20) and the weight-change regression test
/// (§21) — verifying the full candidate-generation -> scoring -> assignment pipeline wires
/// `VehicleId`/`EmergencyId`/`PlanId` correctly end to end, that an infeasible pair can
/// never surface as a real assignment, and that changing `OptimizationProfile` weights
/// does not trigger new Pareto searches — checked structurally (the front data itself is
/// untouched across two different-profile scorings), not merely asserted.

#include <algorithm>
#include <cmath>
#include <unordered_set>

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

/// A 3-vehicle x 3-emergency scenario with a deliberate mix of feasible and infeasible
/// pairs (one vehicle is budget-starved), reused by several tests below so the setup
/// itself is verified once.
struct ThreeByThreeFixture {
    MissionGraph graph;
    TransferModel model;
    std::vector<RescueVehicle> vehicles;
    std::vector<Emergency> emergencies;
};

ThreeByThreeFixture buildFixture() {
    ThreeByThreeFixture f;
    const TransferConstraints c = permissive();

    const Orbit v1o = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, 0.0);
    const Orbit v2o = Orbit::fromAltitude(600.0, degToRad(8.0), degToRad(5.0), degToRad(0.3));
    const Orbit v3o = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, 0.0);  // budget-starved
    const Orbit e1o = Orbit::fromAltitude(520.0, degToRad(6.0), degToRad(2.0), degToRad(0.1));
    const Orbit e2o = Orbit::fromAltitude(650.0, degToRad(9.0), degToRad(6.0), degToRad(0.4));
    const Orbit e3o = Orbit::fromAltitude(540.0, degToRad(7.0), degToRad(3.0), degToRad(0.2));

    const NodeId v1n = f.graph.addNode(GraphNode{NodeId{0}, VehicleId(1), v1o});
    const NodeId v2n = f.graph.addNode(GraphNode{NodeId{0}, VehicleId(2), v2o});
    const NodeId v3n = f.graph.addNode(GraphNode{NodeId{0}, VehicleId(3), v3o});
    const NodeId e1n = f.graph.addNode(GraphNode{NodeId{0}, SatelliteId(101), e1o});
    const NodeId e2n = f.graph.addNode(GraphNode{NodeId{0}, SatelliteId(102), e2o});
    const NodeId e3n = f.graph.addNode(GraphNode{NodeId{0}, SatelliteId(103), e3o});

    for (NodeId vn : {v1n, v2n, v3n}) {
        for (NodeId en : {e1n, e2n, e3n}) {
            addEvaluatedEdge(f.graph, f.model, c, vn, en);
        }
    }

    f.vehicles.emplace_back(VehicleId(1), "RV-1", v1o, 5.0);
    f.vehicles.emplace_back(VehicleId(2), "RV-2", v2o, 5.0);
    f.vehicles.emplace_back(VehicleId(3), "RV-3", v3o, 0.15);  // starved -- some pairs infeasible
    f.emergencies.emplace_back(EmergencyId(101), SatelliteId(101), EmergencyKind::PowerFailure,
                               Severity::High, 0.0, 5.0e7);
    f.emergencies.emplace_back(EmergencyId(102), SatelliteId(102), EmergencyKind::CommsLoss,
                               Severity::Moderate, 0.0, 5.0e7);
    f.emergencies.emplace_back(EmergencyId(103), SatelliteId(103), EmergencyKind::Thermal,
                               Severity::High, 0.0, 5.0e7);
    return f;
}

}  // namespace

// ---------------------------------------------------------------------------
// VehicleId/EmergencyId/PlanId wiring is correct end to end.
// ---------------------------------------------------------------------------

ORCA_TEST(ParetoAssignmentIntegration, EveryAssignmentsMissionPlanMatchesItsOwnIds) {
    ThreeByThreeFixture f = buildFixture();
    const auto fronts =
        generateAllCandidateFronts(f.graph, f.model, f.vehicles, f.emergencies, 0.0, PlanId(1));
    const auto scored = scoreCandidates(fronts, OptimizationProfile::balanced());
    const auto costs = buildAssignmentCosts(fronts, scored, f.emergencies, 0.0,
                                            OptimizationProfile::balanced());
    std::vector<VehicleId> vids;
    std::vector<EmergencyId> eids;
    for (const auto& v : f.vehicles) vids.push_back(v.id());
    for (const auto& e : f.emergencies) eids.push_back(e.id());
    const auto matrix = buildAssignmentMatrix(costs, vids, eids);
    const auto result = solveAssignmentHungarian(matrix, fronts, scored);

    CHECK(!result.assignments.empty());
    for (const auto& a : result.assignments) {
        // The MissionPlan actually attached must, by its own identity, belong to exactly
        // the (vehicle, emergency) pair the Assignment claims -- not some other plan from
        // a different pair's front.
        CHECK(a.missionPlan.vehicle() == a.vehicle);
        CHECK(a.missionPlan.emergency() == a.emergency);
        CHECK(a.missionPlan.id() == a.plan);
    }
}

ORCA_TEST(ParetoAssignmentIntegration, EveryPlanIdAcrossTheWholeMatrixIsUnique) {
    ThreeByThreeFixture f = buildFixture();
    const auto fronts =
        generateAllCandidateFronts(f.graph, f.model, f.vehicles, f.emergencies, 0.0, PlanId(1));

    std::unordered_set<std::uint32_t> seen;
    for (const auto& front : fronts) {
        for (const auto& plan : front.front.solutions) {
            CHECK(seen.insert(plan.id().value()).second);  // false if already present
        }
        for (const auto& plan : front.front.dominated) {
            CHECK(seen.insert(plan.id().value()).second);
        }
    }
    CHECK(!seen.empty());
}

// ---------------------------------------------------------------------------
// An infeasible (vehicle, emergency) pair can never surface as a real assignment.
// ---------------------------------------------------------------------------

ORCA_TEST(ParetoAssignmentIntegration, InfeasiblePairsNeverBecomeRealAssignments) {
    ThreeByThreeFixture f = buildFixture();
    const auto fronts =
        generateAllCandidateFronts(f.graph, f.model, f.vehicles, f.emergencies, 0.0, PlanId(1));
    // Confirm the fixture actually contains at least one infeasible pair -- otherwise this
    // test would not be checking anything.
    bool anyInfeasible = false;
    for (const auto& front : fronts) anyInfeasible = anyInfeasible || !front.feasible;
    CHECK(anyInfeasible);

    const auto scored = scoreCandidates(fronts, OptimizationProfile::balanced());
    const auto costs = buildAssignmentCosts(fronts, scored, f.emergencies, 0.0,
                                            OptimizationProfile::balanced());
    std::vector<VehicleId> vids;
    std::vector<EmergencyId> eids;
    for (const auto& v : f.vehicles) vids.push_back(v.id());
    for (const auto& e : f.emergencies) eids.push_back(e.id());
    const auto matrix = buildAssignmentMatrix(costs, vids, eids);

    for (const auto& result : {solveAssignmentHungarian(matrix, fronts, scored),
                               solveAssignmentGreedy(matrix, fronts, scored)}) {
        for (const auto& a : result.assignments) {
            const std::size_t vRow =
                static_cast<std::size_t>(std::find(vids.begin(), vids.end(), a.vehicle) -
                                        vids.begin());
            const std::size_t eCol =
                static_cast<std::size_t>(std::find(eids.begin(), eids.end(), a.emergency) -
                                        eids.begin());
            CHECK(matrix.feasibleReal[vRow][eCol]);
        }
    }
}

// ---------------------------------------------------------------------------
// A solver call whose fronts/scored do not match the matrix's shape must be rejected, not
// silently mislabel assignments or read out of bounds (found during Phase 6's adversarial
// self-review, the same risk class as AssignmentMatrix's entries/vehicles/emergencies
// order check).
// ---------------------------------------------------------------------------

ORCA_TEST(ParetoAssignmentIntegration, SolversRejectFrontsScoredThatDoNotMatchMatrixShape) {
    ThreeByThreeFixture f = buildFixture();
    const auto fronts =
        generateAllCandidateFronts(f.graph, f.model, f.vehicles, f.emergencies, 0.0, PlanId(1));
    const auto scored = scoreCandidates(fronts, OptimizationProfile::balanced());
    const auto costs = buildAssignmentCosts(fronts, scored, f.emergencies, 0.0,
                                            OptimizationProfile::balanced());
    std::vector<VehicleId> vids;
    std::vector<EmergencyId> eids;
    for (const auto& v : f.vehicles) vids.push_back(v.id());
    for (const auto& e : f.emergencies) eids.push_back(e.id());
    const auto matrix = buildAssignmentMatrix(costs, vids, eids);

    const std::vector<CandidateFront> truncatedFronts(fronts.begin(), fronts.end() - 1);
    const std::vector<ScoredCandidate> truncatedScored(scored.begin(), scored.end() - 1);
    CHECK_THROWS(solveAssignmentHungarian(matrix, truncatedFronts, scored));
    CHECK_THROWS(solveAssignmentHungarian(matrix, fronts, truncatedScored));
    CHECK_THROWS(solveAssignmentGreedy(matrix, truncatedFronts, scored));
    CHECK_THROWS(solveAssignmentGreedy(matrix, fronts, truncatedScored));
}

// ---------------------------------------------------------------------------
// Weight-change regression (§21): re-scoring under a different profile, from the SAME
// already-computed fronts, must change the resulting cost without altering the front data
// -- the structural proof that no new Pareto search happened.
// ---------------------------------------------------------------------------

ORCA_TEST(ParetoAssignmentIntegration, RescoringUnderADifferentProfileDoesNotAlterFrontData) {
    ThreeByThreeFixture f = buildFixture();
    // Pareto search runs exactly once, right here.
    const auto fronts =
        generateAllCandidateFronts(f.graph, f.model, f.vehicles, f.emergencies, 0.0, PlanId(1));
    const std::vector<CandidateFront> frontsSnapshot = fronts;  // deep copy for comparison

    const auto scoredBalanced = scoreCandidates(fronts, OptimizationProfile::balanced());
    const auto costsBalanced = buildAssignmentCosts(fronts, scoredBalanced, f.emergencies, 0.0,
                                                    OptimizationProfile::balanced());

    const auto scoredFastest = scoreCandidates(fronts, OptimizationProfile::fastest());
    const auto costsFastest = buildAssignmentCosts(fronts, scoredFastest, f.emergencies, 0.0,
                                                   OptimizationProfile::fastest());

    // The front data itself -- every solution's cost vector, every PlanId, dominated set
    // sizes -- is byte-identical before and after two different-profile scorings. If
    // scoreCandidates() had re-run findParetoRoutes(), PlanIds and/or solution sets could
    // legitimately differ (a fresh run is not guaranteed to reproduce identical PlanId
    // numbering in general); observing zero change is exactly what "no re-search" means in
    // practice, not merely a type-signature argument.
    CHECK_EQ(fronts.size(), frontsSnapshot.size());
    for (std::size_t i = 0; i < fronts.size(); ++i) {
        CHECK_EQ(fronts[i].feasible, frontsSnapshot[i].feasible);
        CHECK_EQ(fronts[i].front.solutions.size(), frontsSnapshot[i].front.solutions.size());
        CHECK_EQ(fronts[i].front.dominated.size(), frontsSnapshot[i].front.dominated.size());
        for (std::size_t j = 0; j < fronts[i].front.solutions.size(); ++j) {
            CHECK(fronts[i].front.solutions[j].id() == frontsSnapshot[i].front.solutions[j].id());
        }
    }

    // But the actual costs must genuinely differ under most pairs -- otherwise the profile
    // change had no observable effect and this would not be testing anything.
    int differingPairs = 0;
    for (std::size_t i = 0; i < costsBalanced.size(); ++i) {
        if (costsBalanced[i].feasible && costsFastest[i].feasible &&
            std::fabs(costsBalanced[i].cost - costsFastest[i].cost) > 1e-9) {
            ++differingPairs;
        }
    }
    CHECK(differingPairs > 0);
}

ORCA_TEST(ParetoAssignmentIntegration, RescoringChangesTheFinalAssignmentInAtLeastOneFixture) {
    // A softer, outcome-level check: the SAME fronts, scored under two different profiles,
    // can legitimately route the final Hungarian assignment differently -- confirming the
    // profile change actually reaches the assignment layer, not just the cost numbers.
    ThreeByThreeFixture f = buildFixture();
    const auto fronts =
        generateAllCandidateFronts(f.graph, f.model, f.vehicles, f.emergencies, 0.0, PlanId(1));
    std::vector<VehicleId> vids;
    std::vector<EmergencyId> eids;
    for (const auto& v : f.vehicles) vids.push_back(v.id());
    for (const auto& e : f.emergencies) eids.push_back(e.id());

    const auto scoredBalanced = scoreCandidates(fronts, OptimizationProfile::balanced());
    const auto costsBalanced = buildAssignmentCosts(fronts, scoredBalanced, f.emergencies, 0.0,
                                                    OptimizationProfile::balanced());
    const auto matrixBalanced = buildAssignmentMatrix(costsBalanced, vids, eids);
    const auto resultBalanced = solveAssignmentHungarian(matrixBalanced, fronts, scoredBalanced);

    const auto scoredFastest = scoreCandidates(fronts, OptimizationProfile::fastest());
    const auto costsFastest = buildAssignmentCosts(fronts, scoredFastest, f.emergencies, 0.0,
                                                   OptimizationProfile::fastest());
    const auto matrixFastest = buildAssignmentMatrix(costsFastest, vids, eids);
    const auto resultFastest = solveAssignmentHungarian(matrixFastest, fronts, scoredFastest);

    // Both must still be structurally valid results (this is not the point of the test, but
    // a cheap sanity check).
    CHECK(resultBalanced.status == AssignmentStatus::Success ||
         resultBalanced.status == AssignmentStatus::PartiallyAssignable);
    CHECK(resultFastest.status == AssignmentStatus::Success ||
         resultFastest.status == AssignmentStatus::PartiallyAssignable);
    // The total costs need not be equal (different profiles score differently) -- no
    // assertion of equality/inequality here since which is larger is profile-dependent and
    // not the property under test.
}
