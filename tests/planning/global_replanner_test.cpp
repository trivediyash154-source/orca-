/// `evaluateGlobalReplan()` tests (`GlobalReplanner.hpp`) — Phase 7 brief §34, §45: the
/// global (fleet-wide) question, answered by calling straight into Phase 6's own
/// candidate-generation/assignment pipeline, unmodified.

#include "../../cpp/domain/RescueVehicle.hpp"
#include "../../cpp/domain/Emergency.hpp"
#include "../../cpp/graph/MissionGraph.hpp"
#include "../../cpp/planning/GlobalReplanner.hpp"
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
// A new, higher-priority emergency appears while one vehicle is already committed
// elsewhere -- the global replanner considers only the AVAILABLE fleet, exactly the same
// pipeline Phase 6's own assignment_scenarios_test.cpp exercises (brief §34).
// ---------------------------------------------------------------------------

ORCA_TEST(GlobalReplanner, ConsidersOnlyTheSuppliedAvailableVehiclesAndEmergencies) {
    MissionGraph g;
    TransferModel model;
    const TransferConstraints c = permissive();

    // V2 is the only "available" vehicle passed in -- representing V1 already being
    // committed to an active mission elsewhere and therefore never offered here.
    const Orbit v2o = Orbit::fromAltitude(700.0, degToRad(15.0), degToRad(5.0), degToRad(0.3));
    const Orbit e1o = Orbit::fromAltitude(750.0, degToRad(16.0), degToRad(6.0), degToRad(0.35));
    const NodeId v2n = g.addNode(GraphNode{NodeId{0}, VehicleId(2), v2o});
    const NodeId e1n = g.addNode(GraphNode{NodeId{0}, SatelliteId(101), e1o});
    addEvaluatedEdge(g, model, c, v2n, e1n);

    const RescueVehicle rv2(VehicleId(2), "RV-2", v2o, 5.0);
    const Emergency e1(EmergencyId(101), SatelliteId(101), EmergencyKind::PowerFailure,
                       Severity::Critical, 0.0, 5.0e7);
    const std::vector<RescueVehicle> available{rv2};
    const std::vector<Emergency> pending{e1};

    PlanId nextPlanId(1);
    const AssignmentResult result =
        evaluateGlobalReplan(g, model, available, pending, 0.0, OptimizationProfile::balanced(),
                             nextPlanId);

    CHECK(result.status == AssignmentStatus::Success);
    CHECK_EQ(result.assignments.size(), std::size_t{1});
    CHECK_EQ(result.assignments[0].vehicle.value(), std::uint32_t{2});
    CHECK_EQ(result.assignments[0].emergency.value(), std::uint32_t{101});
    // PlanId advanced past what was consumed -- the caller can safely generate more.
    CHECK(nextPlanId.value() > 1);
}

ORCA_TEST(GlobalReplanner, TwoVehiclesTwoEmergenciesUsesRealHungarianOptimization) {
    MissionGraph g;
    TransferModel model;
    const TransferConstraints c = permissive();

    const Orbit v1o = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, 0.0);
    const Orbit v2o = Orbit::fromAltitude(700.0, degToRad(15.0), degToRad(10.0), degToRad(0.3));
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
    const std::vector<RescueVehicle> available{rv1, rv2};
    const std::vector<Emergency> pending{e1, e2};

    PlanId nextPlanId(1);
    const AssignmentResult result =
        evaluateGlobalReplan(g, model, available, pending, 0.0, OptimizationProfile::balanced(),
                             nextPlanId);

    CHECK(result.status == AssignmentStatus::Success);
    CHECK_EQ(result.assignments.size(), std::size_t{2});
    // Matches assignment_scenarios_test.cpp's ScenarioB numbers exactly -- same geometry.
    CHECK_NEAR(result.totalCost, 1.2532, 1e-3);
}

ORCA_TEST(GlobalReplanner, RejectsEmptyAvailableVehicles) {
    MissionGraph g;
    TransferModel model;
    const Emergency e1(EmergencyId(101), SatelliteId(101), EmergencyKind::PowerFailure,
                       Severity::High, 0.0, 5.0e7);
    PlanId nextPlanId(1);
    CHECK_THROWS(evaluateGlobalReplan(g, model, {}, {e1}, 0.0, OptimizationProfile::balanced(),
                                      nextPlanId));
}

ORCA_TEST(GlobalReplanner, RejectsAVehicleThatIsAlreadyCommittedToAnActiveMission) {
    // Found during Phase 7's adversarial self-review: nothing previously stopped a caller
    // from accidentally offering an already-busy vehicle to a global replan, silently
    // double-booking it onto a second, unrelated emergency.
    MissionGraph g;
    TransferModel model;
    const Orbit v1o = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, 0.0);
    const Orbit e1o = Orbit::fromAltitude(520.0, degToRad(6.0), degToRad(2.0), degToRad(0.1));
    const NodeId v1n = g.addNode(GraphNode{NodeId{0}, VehicleId(1), v1o});
    const NodeId e1n = g.addNode(GraphNode{NodeId{0}, SatelliteId(101), e1o});
    addEvaluatedEdge(g, model, permissive(), v1n, e1n);

    RescueVehicle busy(VehicleId(1), "RV-1", v1o, 5.0);
    busy.setStatus(VehicleStatus::Assigned);  // already committed elsewhere

    const Emergency e1(EmergencyId(101), SatelliteId(101), EmergencyKind::PowerFailure,
                       Severity::High, 0.0, 5.0e7);
    PlanId nextPlanId(1);
    CHECK_THROWS(evaluateGlobalReplan(g, model, {busy}, {e1}, 0.0, OptimizationProfile::balanced(),
                                      nextPlanId));
}
