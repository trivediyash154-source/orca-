/// Direct tests of `evaluateMissionValidity()` / `evaluateRemainingRouteCost()`
/// (`MissionValidity.hpp`) — Phase 7 brief §7-8. Every number below came from actually
/// compiling and running the exact scenario.

#include "../../cpp/domain/RescueVehicle.hpp"
#include "../../cpp/domain/Emergency.hpp"
#include "../../cpp/domain/Mission.hpp"
#include "../../cpp/graph/MissionGraph.hpp"
#include "../../cpp/optimization/CandidateGeneration.hpp"
#include "../../cpp/planning/MissionValidity.hpp"
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

/// A single-leg V1->E1 scenario, matching the exact numbers used throughout this file:
/// route legs=1, time=526940.15s, fuel=0.0293, risk=0.3034 under a 5.0 km/s vehicle.
struct SingleLegFixture {
    MissionGraph graph;
    TransferModel model;
    RescueVehicle vehicle;
    Emergency emergency;
    MissionPlan plan;
};

SingleLegFixture buildSingleLegFixture(KmPerSec vehicleBudget, Seconds survivalWindowSec) {
    MissionGraph g;
    TransferModel model;
    const TransferConstraints c = permissive();
    const Orbit v1o = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, 0.0);
    const Orbit e1o = Orbit::fromAltitude(550.0, degToRad(6.0), degToRad(5.0), degToRad(0.1));
    const NodeId v1n = g.addNode(GraphNode{NodeId{0}, VehicleId(1), v1o});
    const NodeId e1n = g.addNode(GraphNode{NodeId{0}, SatelliteId(101), e1o});
    addEvaluatedEdge(g, model, c, v1n, e1n);

    RescueVehicle rv1(VehicleId(1), "RV-1", v1o, vehicleBudget);
    Emergency e1(EmergencyId(101), SatelliteId(101), EmergencyKind::PowerFailure, Severity::High,
                0.0, survivalWindowSec);

    PlanId nextPlanId(1);
    EntityNodeIndex index = buildEntityNodeIndex(g);
    CandidateFront front = generateCandidateFront(g, model, index, rv1, e1, 0.0, nextPlanId);
    const std::optional<RecommendedPlan> recommended =
        selectRecommendedPlan(front.front, OptimizationProfile::balanced());
    return SingleLegFixture{std::move(g), std::move(model), std::move(rv1), std::move(e1),
                            front.front.solutions[recommended->planIndex]};
}

}  // namespace

ORCA_TEST(MissionValidity, FreshMissionWithFullBudgetIsValid) {
    SingleLegFixture f = buildSingleLegFixture(5.0, 5.0e7);
    Mission mission(MissionId(1), f.plan, 0.0);
    mission.setState(MissionState::Launched);
    mission.setState(MissionState::InTransit);

    const MissionValidity v =
        evaluateMissionValidity(mission, f.vehicle, f.emergency, f.graph, f.model, 100.0);
    CHECK(v.state == ValidityState::Valid);
    CHECK(v.remaining.feasible);
    CHECK_NEAR(v.remaining.cost.risk, 0.3034, 1e-3);
}

ORCA_TEST(MissionValidity, InsufficientRemainingDeltaVIsInvalid) {
    SingleLegFixture f = buildSingleLegFixture(5.0, 5.0e7);
    Mission mission(MissionId(1), f.plan, 0.0);
    mission.setState(MissionState::Launched);
    mission.setState(MissionState::InTransit);

    RescueVehicle starved(VehicleId(1), "RV-1", f.vehicle.orbit(), 5.0);
    starved.consumeDeltaV(4.999);  // leaves far less than the ~0.147 km/s this leg needs

    const MissionValidity v =
        evaluateMissionValidity(mission, starved, f.emergency, f.graph, f.model, 200.0);
    CHECK(v.state == ValidityState::Invalid);
    CHECK(v.reason == ReplanReason::DeltaVConstraint);
    CHECK(v.violatedConstraint == ConstraintKind::DeltaVBudget);
    CHECK(!v.remaining.feasible);
}

ORCA_TEST(MissionValidity, TightenedDeadlineIsInvalidWithSurvivalWindowReason) {
    SingleLegFixture f = buildSingleLegFixture(5.0, 5.0e7);
    Mission mission(MissionId(1), f.plan, 0.0);
    mission.setState(MissionState::Launched);
    mission.setState(MissionState::InTransit);

    // The leg's own arrival is ~526940s after departure; a deadline far tighter than that
    // makes the same route unreachable in time.
    Emergency tightened(EmergencyId(101), SatelliteId(101), EmergencyKind::PowerFailure,
                       Severity::High, 0.0, 1000.0);
    const MissionValidity v =
        evaluateMissionValidity(mission, f.vehicle, tightened, f.graph, f.model, 0.0);
    CHECK(v.state == ValidityState::Invalid);
    CHECK(v.reason == ReplanReason::DeadlineThreat);
    CHECK(v.violatedConstraint == ConstraintKind::SurvivalWindow);
}

ORCA_TEST(MissionValidity, ExcessiveRiskIsInvalidEvenWhenPhysicallyFeasible) {
    SingleLegFixture f = buildSingleLegFixture(5.0, 5.0e7);
    Mission mission(MissionId(1), f.plan, 0.0);
    mission.setState(MissionState::Launched);
    mission.setState(MissionState::InTransit);

    // Real risk (~0.3034) is well under the default 0.85 ceiling but well over a strict
    // policy ceiling -- risk enforcement is a POLICY layer on top of raw feasibility.
    ReplanPolicy strictRisk = defaultReplanPolicy();
    strictRisk.maxAcceptableRisk = 0.1;
    const MissionValidity v =
        evaluateMissionValidity(mission, f.vehicle, f.emergency, f.graph, f.model, 0.0, strictRisk);
    CHECK(v.state == ValidityState::Invalid);
    CHECK(v.reason == ReplanReason::RiskConstraint);
    CHECK(v.violatedConstraint == ConstraintKind::RiskCeiling);
    CHECK(v.remaining.feasible);  // physically fine -- the policy ceiling is what fired
}

ORCA_TEST(MissionValidity, ResolvedEmergencyIsCompleted) {
    SingleLegFixture f = buildSingleLegFixture(5.0, 5.0e7);
    Mission mission(MissionId(1), f.plan, 0.0);
    mission.setState(MissionState::Launched);
    mission.setState(MissionState::InTransit);

    Emergency resolved = f.emergency;
    resolved.setState(EmergencyState::Assigned);
    resolved.setState(EmergencyState::InProgress);
    resolved.setState(EmergencyState::Resolved);

    const MissionValidity v =
        evaluateMissionValidity(mission, f.vehicle, resolved, f.graph, f.model, 100.0);
    CHECK(v.state == ValidityState::Completed);
}

ORCA_TEST(MissionValidity, DisabledVehicleIsInvalidWithVehicleUnavailableReason) {
    SingleLegFixture f = buildSingleLegFixture(5.0, 5.0e7);
    Mission mission(MissionId(1), f.plan, 0.0);
    mission.setState(MissionState::Launched);
    mission.setState(MissionState::InTransit);

    RescueVehicle disabled(VehicleId(1), "RV-1", f.vehicle.orbit(), 5.0);
    disabled.setStatus(VehicleStatus::Disabled);

    const MissionValidity v =
        evaluateMissionValidity(mission, disabled, f.emergency, f.graph, f.model, 100.0);
    CHECK(v.state == ValidityState::Invalid);
    CHECK(v.reason == ReplanReason::VehicleUnavailable);
    CHECK(v.violatedConstraint == ConstraintKind::NoAsset);
}

ORCA_TEST(MissionValidity, ThinDeltaVMarginIsWarningNotInvalid) {
    SingleLegFixture f = buildSingleLegFixture(5.0, 5.0e7);
    Mission mission(MissionId(1), f.plan, 0.0);
    mission.setState(MissionState::Launched);
    mission.setState(MissionState::InTransit);

    // The leg needs ~0.147 km/s; leave just over that (feasible) but under the default
    // 1.15x warning margin.
    const KmPerSec required = f.plan.totalCost().fuel * f.vehicle.deltaVBudgetKms();
    RescueVehicle thin(VehicleId(1), "RV-1", f.vehicle.orbit(), 5.0);
    thin.consumeDeltaV(5.0 - required * 1.05);

    const MissionValidity v =
        evaluateMissionValidity(mission, thin, f.emergency, f.graph, f.model, 0.0);
    CHECK(v.state == ValidityState::Warning);
    CHECK(v.reason == ReplanReason::DeltaVConstraint);
}

ORCA_TEST(MissionValidity, RejectsTerminalMission) {
    SingleLegFixture f = buildSingleLegFixture(5.0, 5.0e7);
    Mission mission(MissionId(1), f.plan, 0.0);
    mission.setState(MissionState::Aborted);

    CHECK_THROWS(
        evaluateMissionValidity(mission, f.vehicle, f.emergency, f.graph, f.model, 100.0));
}

ORCA_TEST(MissionValidity, RemainingLegStartIndexSkipsAnInProgressLeg) {
    CHECK_EQ(remainingLegStartIndex(0, 0.0), std::size_t{0});
    CHECK_EQ(remainingLegStartIndex(0, 0.4), std::size_t{1});
    CHECK_EQ(remainingLegStartIndex(2, 0.0), std::size_t{2});
    CHECK_EQ(remainingLegStartIndex(2, 0.99), std::size_t{3});
}

ORCA_TEST(MissionValidity, EveryLegAlreadyFlownIsTriviallyFeasible) {
    SingleLegFixture f = buildSingleLegFixture(5.0, 5.0e7);
    const RemainingRouteEvaluation eval = evaluateRemainingRouteCost(
        f.graph, f.model, f.plan.route(), f.plan.route().size(), 0.0, permissive());
    CHECK(eval.feasible);
    CHECK_NEAR(eval.cost.time, 0.0, 1e-12);
}

ORCA_TEST(MissionValidity, RejectsStartLegIndexBeyondRouteLength) {
    SingleLegFixture f = buildSingleLegFixture(5.0, 5.0e7);
    CHECK_THROWS(evaluateRemainingRouteCost(f.graph, f.model, f.plan.route(),
                                            f.plan.route().size() + 1, 0.0, permissive()));
}

// ---------------------------------------------------------------------------
// Phase 8: externally invalidated edges and per-leg arrival timing.
// ---------------------------------------------------------------------------

ORCA_TEST(MissionValidity, InvalidatedEdgeForcesInfeasibilityRegardlessOfPhysics) {
    SingleLegFixture f = buildSingleLegFixture(5.0, 5.0e7);
    CHECK(f.plan.route().size() == 1);
    const EdgeId theOnlyEdge = f.plan.route()[0];

    const RemainingRouteEvaluation withoutBlock = evaluateRemainingRouteCost(
        f.graph, f.model, f.plan.route(), 0, 0.0, permissive());
    CHECK(withoutBlock.feasible);

    const RemainingRouteEvaluation withBlock = evaluateRemainingRouteCost(
        f.graph, f.model, f.plan.route(), 0, 0.0, permissive(), theOnlyEdge);
    CHECK(!withBlock.feasible);
    CHECK(withBlock.infeasibilityReason == ConstraintKind::Unreachable);
}

ORCA_TEST(MissionValidity, InvalidatedEdgeThatIsNotOnTheRouteHasNoEffect) {
    SingleLegFixture f = buildSingleLegFixture(5.0, 5.0e7);
    const EdgeId someOtherEdge(f.plan.route()[0].value() + 1000);
    const RemainingRouteEvaluation eval = evaluateRemainingRouteCost(
        f.graph, f.model, f.plan.route(), 0, 0.0, permissive(), someOtherEdge);
    CHECK(eval.feasible);
}

ORCA_TEST(MissionValidity, EvaluateMissionValidityHonoursInvalidatedEdge) {
    SingleLegFixture f = buildSingleLegFixture(5.0, 5.0e7);
    Mission mission(MissionId(1), f.plan, 0.0);
    mission.setState(MissionState::Launched);
    mission.setState(MissionState::InTransit);

    const MissionValidity blocked = evaluateMissionValidity(
        mission, f.vehicle, f.emergency, f.graph, f.model, 0.0, defaultReplanPolicy(),
        f.plan.route()[0]);
    CHECK(blocked.state == ValidityState::Invalid);
    CHECK(blocked.reason == ReplanReason::RouteInvalidated);
}

ORCA_TEST(MissionValidity, LegArrivalTimesOutIsPopulatedPerLeg) {
    SingleLegFixture f = buildSingleLegFixture(5.0, 5.0e7);
    std::vector<Seconds> legArrivals;
    const RemainingRouteEvaluation eval = evaluateRemainingRouteCost(
        f.graph, f.model, f.plan.route(), 0, 0.0, permissive(), std::nullopt, &legArrivals);
    CHECK(eval.feasible);
    CHECK_EQ(legArrivals.size(), f.plan.route().size());
    CHECK_NEAR(legArrivals.back(), eval.arrivalTimeSec, 1e-9);
}
