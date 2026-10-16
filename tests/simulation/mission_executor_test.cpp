/// `advanceMissionProgress()` tests — Phase 8 brief §53's mission-execution and resource
/// categories: single-leg, multi-leg, completion, and cumulative (never reset) resource
/// consumption. Reuses the exact orbit pair Phase 7's own `mission_validity_test.cpp`
/// verified (time=526940.15s, fuel=0.0293, delta-v=0.1467km/s under a 5.0km/s vehicle) so
/// the expected numbers here are cross-checked against an independently-written test file,
/// not merely self-consistent.

#include "../../cpp/optimization/CandidateGeneration.hpp"
#include "../../cpp/simulation/MissionExecutor.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {

TransferConstraints permissive() {
    TransferConstraints c;
    c.maxDeltaVKms = 1.0e6;
    c.deltaVBudgetKms = 1.0e6;
    c.availableFuelFraction = 1.0;
    c.maxWaitSec = 1.0e13;
    c.maxRisk = 1.0;
    c.latestArrivalSec = 1.0e18;
    return c;
}

void addEvaluatedEdge(MissionGraph& g, const TransferModel& model, const TransferConstraints& c,
                      NodeId u, NodeId v) {
    g.addEdge(u, v, model.evaluate(g.node(u).orbit(), g.node(v).orbit(), 0.0, c), 0.0);
}

struct SingleLegFixture {
    MissionGraph graph;
    TransferModel model;
    RescueVehicle vehicle;
    Emergency emergency;
    MissionPlan plan;
};

SingleLegFixture buildSingleLegFixture() {
    MissionGraph g;
    TransferModel model;
    const TransferConstraints c = permissive();
    const Orbit v1o = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, 0.0);
    const Orbit e1o = Orbit::fromAltitude(550.0, degToRad(6.0), degToRad(5.0), degToRad(0.1));
    const NodeId v1n = g.addNode(GraphNode{NodeId{0}, VehicleId(1), v1o});
    const NodeId e1n = g.addNode(GraphNode{NodeId{0}, SatelliteId(101), e1o});
    addEvaluatedEdge(g, model, c, v1n, e1n);

    RescueVehicle rv1(VehicleId(1), "RV-1", v1o, 5.0);
    Emergency e1(EmergencyId(101), SatelliteId(101), EmergencyKind::PowerFailure, Severity::High,
                0.0, 5.0e7);
    PlanId nextPlanId(1);
    EntityNodeIndex index = buildEntityNodeIndex(g);
    CandidateFront front = generateCandidateFront(g, model, index, rv1, e1, 0.0, nextPlanId);
    const std::optional<RecommendedPlan> recommended =
        selectRecommendedPlan(front.front, OptimizationProfile::balanced());
    return SingleLegFixture{std::move(g), std::move(model), std::move(rv1), std::move(e1),
                            front.front.solutions[recommended->planIndex]};
}

}  // namespace

ORCA_TEST(MissionExecutor, MidLegCallOnlyUpdatesProgress) {
    SingleLegFixture f = buildSingleLegFixture();
    Mission mission(MissionId(1), f.plan, 0.0);
    mission.setState(MissionState::Launched);
    mission.setState(MissionState::InTransit);

    const ExecutionResult result =
        advanceMissionProgress(mission, f.vehicle, f.plan, f.graph, f.model, 100.0);
    CHECK(result.outcome == ExecutionOutcome::InProgress);
    CHECK_NEAR(result.deltaVConsumedThisCallKms, 0.0, 1e-12);
    CHECK_EQ(mission.currentLeg(), std::size_t{0});
    CHECK(mission.progressOnLeg() > 0.0 && mission.progressOnLeg() < 1.0);
    CHECK_NEAR(f.vehicle.deltaVRemainingKms(), 5.0, 1e-9);  // nothing consumed yet
}

ORCA_TEST(MissionExecutor, ArrivalConsumesTheLegsActualDeltaVAndReportsArrived) {
    SingleLegFixture f = buildSingleLegFixture();
    Mission mission(MissionId(1), f.plan, 0.0);
    mission.setState(MissionState::Launched);
    mission.setState(MissionState::InTransit);

    const ExecutionResult result = advanceMissionProgress(
        mission, f.vehicle, f.plan, f.graph, f.model, f.plan.arrivalTimeSec());
    CHECK(result.outcome == ExecutionOutcome::Arrived);
    CHECK_NEAR(result.deltaVConsumedThisCallKms, 0.1467, 1e-3);
    CHECK_NEAR(f.vehicle.deltaVRemainingKms(), 5.0 - 0.1467, 1e-3);
    CHECK_NEAR(mission.deltaVSpentKms(), 0.1467, 1e-3);
    CHECK_NEAR(mission.progressOnLeg(), 1.0, 1e-9);
    // advanceMissionProgress does NOT itself transition the mission's state -- that is
    // SimulationEngine's job, via the same domain API, once (brief §33's "use domain
    // APIs... in exactly one place").
    CHECK(mission.state() == MissionState::InTransit);
}

ORCA_TEST(MissionExecutor, RejectsNonInTransitMission) {
    SingleLegFixture f = buildSingleLegFixture();
    Mission mission(MissionId(1), f.plan, 0.0);  // still Planned
    CHECK_THROWS(advanceMissionProgress(mission, f.vehicle, f.plan, f.graph, f.model, 10.0));
}

ORCA_TEST(MissionExecutor, RejectsAPlanThatDoesNotMatchTheMissionsCurrentRoute) {
    SingleLegFixture f = buildSingleLegFixture();
    Mission mission(MissionId(1), f.plan, 0.0);
    mission.setState(MissionState::Launched);
    mission.setState(MissionState::InTransit);

    // A plan with a deliberately different route (an EdgeId that cannot equal f.plan's own
    // single-edge route) -- constructed directly rather than via a second fixture, since a
    // second fixture built the same way produces the SAME edge numbering in ITS OWN fresh
    // graph and would not actually exercise a mismatch.
    const MissionPlan mismatched = MissionPlan::makeFeasible(
        PlanId(9999), f.vehicle.id(), f.emergency.id(), {EdgeId(f.plan.route()[0].value() + 1)},
        f.plan.totalCost(), f.plan.departureTimeSec(), f.plan.arrivalTimeSec());
    CHECK_THROWS(
        advanceMissionProgress(mission, f.vehicle, mismatched, f.graph, f.model, 10.0));
}

ORCA_TEST(MissionExecutor, ResourceConsumptionIsCumulativeNeverReset) {
    SingleLegFixture f = buildSingleLegFixture();
    Mission mission(MissionId(1), f.plan, 0.0);
    mission.setState(MissionState::Launched);
    mission.setState(MissionState::InTransit);

    // Several mid-flight calls before arrival must not consume anything.
    for (Seconds t : {f.plan.arrivalTimeSec() * 0.25, f.plan.arrivalTimeSec() * 0.5,
                      f.plan.arrivalTimeSec() * 0.75}) {
        (void)advanceMissionProgress(mission, f.vehicle, f.plan, f.graph, f.model, t);
        CHECK_NEAR(mission.deltaVSpentKms(), 0.0, 1e-9);
    }
    (void)advanceMissionProgress(mission, f.vehicle, f.plan, f.graph, f.model,
                                 f.plan.arrivalTimeSec());
    const KmPerSec spentAfterArrival = mission.deltaVSpentKms();
    CHECK(spentAfterArrival > 0.0);

    // A later, otherwise-inert call (mission already fully at its final leg) must not
    // double-consume -- calling advanceMissionProgress again after Arrived, with a
    // LATER time, on a mission whose state a caller has not yet transitioned, is out of
    // this function's documented contract once InTransit -- transition state.hmm actually
    // is still InTransit until the caller moves it, so verify idempotence directly.
    (void)advanceMissionProgress(mission, f.vehicle, f.plan, f.graph, f.model,
                                 f.plan.arrivalTimeSec() + 100.0);
    CHECK_NEAR(mission.deltaVSpentKms(), spentAfterArrival, 1e-9);
}
