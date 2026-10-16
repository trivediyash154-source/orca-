/// `DynamicReplanner::evaluate()` / `evaluatePreemption()` tests — Phase 7 brief §31-43's
/// scenarios A-J plus hysteresis, idempotence, determinism, mission progress, deadline,
/// fuel/delta-v, target condition, vehicle availability and communication-loss coverage.
/// Every number below came from actually compiling and running the exact scenario.

#include "../../cpp/domain/RescueVehicle.hpp"
#include "../../cpp/domain/Emergency.hpp"
#include "../../cpp/domain/Mission.hpp"
#include "../../cpp/graph/MissionGraph.hpp"
#include "../../cpp/optimization/CandidateGeneration.hpp"
#include "../../cpp/planning/DynamicReplanner.hpp"
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

/// The single-leg V1->E1 fixture reused across several tests (`mission_validity_test.cpp`'s
/// exact numbers: legs=1, time=526940.15s, fuel=0.0293, risk=0.3034 under a 5.0 km/s
/// vehicle, no alternative path -- only one edge exists.
struct SingleLegFixture {
    MissionGraph graph;
    TransferModel model;
    RescueVehicle vehicle;
    Emergency emergency;
    MissionPlan plan;
};

SingleLegFixture buildSingleLegFixture(KmPerSec vehicleBudget = 5.0,
                                       Seconds survivalWindowSec = 5.0e7) {
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

Mission launchedMission(MissionId id, const MissionPlan& plan) {
    Mission m(id, plan, 0.0);
    m.setState(MissionState::Launched);
    m.setState(MissionState::InTransit);
    return m;
}

/// The two-alternative "diamond" fixture (`ScoresCollide`-style, reused from Phase 6's own
/// front-shape) -- fast/expensive-time vs slow/cheap-time route to the same emergency, so
/// tightening the deadline or asking for a switch has a real alternative to find.
struct DiamondFixture {
    MissionGraph graph;
    TransferModel model;
    RescueVehicle vehicle;
    Emergency emergency;
    ParetoFront front;
};

DiamondFixture buildDiamondFixture() {
    MissionGraph g;
    TransferModel model;
    const TransferConstraints c = permissive();
    const Orbit v1o = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, 0.0);
    const Orbit fastMidO = Orbit::fromAltitude(560.0, degToRad(6.0), 0.0, degToRad(0.2));
    const Orbit cheapMidO = Orbit::fromAltitude(520.0, degToRad(5.5), 0.0, degToRad(1.3));
    const Orbit e1o = Orbit::fromAltitude(600.0, degToRad(7.0), 0.0, degToRad(2.5));
    const NodeId v1n = g.addNode(GraphNode{NodeId{0}, VehicleId(1), v1o});
    const NodeId e1n = g.addNode(GraphNode{NodeId{0}, SatelliteId(101), e1o});
    const NodeId fastN = g.addNode(GraphNode{NodeId{0}, SatelliteId(201), fastMidO});
    const NodeId cheapN = g.addNode(GraphNode{NodeId{0}, SatelliteId(202), cheapMidO});
    addEvaluatedEdge(g, model, c, v1n, fastN);
    addEvaluatedEdge(g, model, c, fastN, e1n);
    addEvaluatedEdge(g, model, c, v1n, cheapN);
    addEvaluatedEdge(g, model, c, cheapN, e1n);

    RescueVehicle rv1(VehicleId(1), "RV-1", v1o, 10.0);
    Emergency e1(EmergencyId(101), SatelliteId(101), EmergencyKind::PowerFailure, Severity::High,
                0.0, 5.0e7);

    PlanId nextPlanId(1);
    EntityNodeIndex index = buildEntityNodeIndex(g);
    CandidateFront front = generateCandidateFront(g, model, index, rv1, e1, 0.0, nextPlanId);
    return DiamondFixture{std::move(g), std::move(model), std::move(rv1), std::move(e1),
                          std::move(front.front)};
}

std::size_t slowestSolutionIndex(const ParetoFront& front) {
    std::size_t idx = 0;
    for (std::size_t i = 1; i < front.solutions.size(); ++i) {
        if (front.solutions[i].totalCost().time > front.solutions[idx].totalCost().time) idx = i;
    }
    return idx;
}

}  // namespace

// ---------------------------------------------------------------------------
// Scenario A: current plan remains valid -- Continue.
// ---------------------------------------------------------------------------

ORCA_TEST(DynamicReplanner, ScenarioA_CurrentPlanRemainsValid_Continue) {
    SingleLegFixture f = buildSingleLegFixture();
    Mission mission = launchedMission(MissionId(1), f.plan);
    DynamicReplanner replanner;
    PlanId nextPlanId(1000);
    MissionChange trivial{.kind = ChangeKind::FuelChanged, .atSec = 100.0, .magnitudeFraction = 0.001};

    const ReplanDecision d = replanner.evaluate(mission, f.vehicle, f.emergency, f.graph, f.model,
                                                100.0, trivial, OptimizationProfile::balanced(),
                                                nextPlanId);
    CHECK(d.decision == ReplanDecisionType::Continue);
    CHECK(d.validity.state == ValidityState::Valid);
}

// ---------------------------------------------------------------------------
// Scenario B: current route becomes infeasible, but an alternative exists -- Replan.
// ---------------------------------------------------------------------------

ORCA_TEST(DynamicReplanner, ScenarioB_RouteBecomesInfeasible_Replan) {
    DiamondFixture f = buildDiamondFixture();
    const MissionPlan currentPlan = f.front.solutions[slowestSolutionIndex(f.front)];
    Mission mission = launchedMission(MissionId(1), currentPlan);

    Seconds fastestArrival = 1.0e18;
    for (const auto& sol : f.front.solutions) {
        fastestArrival = std::min(fastestArrival, sol.arrivalTimeSec());
    }
    const Seconds tightDeadline = (fastestArrival + currentPlan.arrivalTimeSec()) / 2.0;
    Emergency tightened(EmergencyId(101), SatelliteId(101), EmergencyKind::PowerFailure,
                       Severity::High, 0.0, tightDeadline);

    DynamicReplanner replanner;
    PlanId nextPlanId(1000);
    MissionChange deadlineChange{.kind = ChangeKind::DeadlineChanged, .atSec = 5.0};
    const ReplanDecision d = replanner.evaluate(mission, f.vehicle, tightened, f.graph, f.model,
                                                5.0, deadlineChange, OptimizationProfile::balanced(),
                                                nextPlanId);
    CHECK(d.decision == ReplanDecisionType::Replan);
    CHECK(d.reason == ReplanReason::DeadlineThreat);
    CHECK(d.newPlan.has_value());
    CHECK(d.newPlan->arrivalTimeSec() <= tightDeadline + 1e-6);
}

// ---------------------------------------------------------------------------
// Scenario C: no feasible alternative exists -- Abort.
// ---------------------------------------------------------------------------

ORCA_TEST(DynamicReplanner, ScenarioC_NoFeasibleAlternative_Abort) {
    SingleLegFixture f = buildSingleLegFixture();
    Mission mission = launchedMission(MissionId(1), f.plan);

    RescueVehicle starved(VehicleId(1), "RV-1", f.vehicle.orbit(), 5.0);
    starved.consumeDeltaV(4.999);

    DynamicReplanner replanner;
    PlanId nextPlanId(1000);
    MissionChange fuelLoss{.kind = ChangeKind::FuelChanged, .atSec = 200.0, .magnitudeFraction = 0.99};
    const ReplanDecision d = replanner.evaluate(mission, starved, f.emergency, f.graph, f.model,
                                                200.0, fuelLoss, OptimizationProfile::balanced(),
                                                nextPlanId);
    CHECK(d.decision == ReplanDecisionType::Abort);
    CHECK(d.abortReason == AbortReason::InsufficientFuel);
    CHECK(!d.newPlan.has_value());
}

// ---------------------------------------------------------------------------
// Scenario D: mission already completed -- Complete.
// ---------------------------------------------------------------------------

ORCA_TEST(DynamicReplanner, ScenarioD_MissionAlreadyCompleted_Complete) {
    SingleLegFixture f = buildSingleLegFixture();
    Mission mission = launchedMission(MissionId(1), f.plan);

    Emergency resolved = f.emergency;
    resolved.setState(EmergencyState::Assigned);
    resolved.setState(EmergencyState::InProgress);
    resolved.setState(EmergencyState::Resolved);

    DynamicReplanner replanner;
    PlanId nextPlanId(1000);
    MissionChange manual{.kind = ChangeKind::ManualReplan, .atSec = 1000.0};
    const ReplanDecision d = replanner.evaluate(mission, f.vehicle, resolved, f.graph, f.model,
                                                1000.0, manual, OptimizationProfile::balanced(),
                                                nextPlanId);
    CHECK(d.decision == ReplanDecisionType::Complete);
}

// ---------------------------------------------------------------------------
// Scenario G/H: substantial fuel loss replans; tiny fuel loss does not (materiality gate,
// brief §12).
// ---------------------------------------------------------------------------

ORCA_TEST(DynamicReplanner, ScenarioG_SubstantialFuelLossTriggersMandatoryReplan) {
    SingleLegFixture f = buildSingleLegFixture();
    Mission mission = launchedMission(MissionId(1), f.plan);

    RescueVehicle starved(VehicleId(1), "RV-1", f.vehicle.orbit(), 5.0);
    starved.consumeDeltaV(4.99);  // leaves far less than the ~0.147 km/s this leg needs

    DynamicReplanner replanner;
    PlanId nextPlanId(1000);
    MissionChange bigLoss{.kind = ChangeKind::FuelChanged, .atSec = 10.0, .magnitudeFraction = 0.998};
    const ReplanDecision d = replanner.evaluate(mission, starved, f.emergency, f.graph, f.model,
                                                10.0, bigLoss, OptimizationProfile::balanced(),
                                                nextPlanId);
    CHECK(d.validity.state == ValidityState::Invalid);
    CHECK(d.decision != ReplanDecisionType::Continue);
}

ORCA_TEST(DynamicReplanner, ScenarioH_TinyFuelLossDoesNotTriggerUnnecessaryReplan) {
    SingleLegFixture f = buildSingleLegFixture();
    Mission mission = launchedMission(MissionId(1), f.plan);

    DynamicReplanner replanner;
    PlanId nextPlanId(1000);
    // Vehicle unchanged (still full budget); a tiny reported magnitude must not even
    // trigger a search for an alternative.
    MissionChange tinyLoss{.kind = ChangeKind::FuelChanged, .atSec = 10.0, .magnitudeFraction = 0.0001};
    const ReplanDecision d = replanner.evaluate(mission, f.vehicle, f.emergency, f.graph, f.model,
                                                10.0, tinyLoss, OptimizationProfile::balanced(),
                                                nextPlanId);
    CHECK(d.decision == ReplanDecisionType::Continue);
    CHECK_EQ(replanner.metrics().candidatePlansGenerated, 0);
}

// ---------------------------------------------------------------------------
// Scenario I/J: trivial difference retains the plan; a materially better one switches.
// ---------------------------------------------------------------------------

ORCA_TEST(DynamicReplanner, ScenarioJ_MaterialSafetyImprovementSwitches) {
    DiamondFixture f = buildDiamondFixture();
    const MissionPlan currentPlan = f.front.solutions[slowestSolutionIndex(f.front)];
    Mission mission = launchedMission(MissionId(1), currentPlan);

    DynamicReplanner replanner;
    PlanId nextPlanId(1000);
    MissionChange manual{.kind = ChangeKind::ManualReplan, .atSec = 5.0};
    const ReplanDecision d = replanner.evaluate(mission, f.vehicle, f.emergency, f.graph, f.model,
                                                5.0, manual, OptimizationProfile::balanced(),
                                                nextPlanId);
    CHECK(d.validity.state == ValidityState::Valid);
    CHECK(d.decision == ReplanDecisionType::Replan);
    CHECK(d.reason == ReplanReason::ManualReplan);
    CHECK(d.newPlan.has_value());
    CHECK(d.newPlan->totalCost().time < currentPlan.totalCost().time);
}

ORCA_TEST(DynamicReplanner, ScenarioI_TrivialDifferenceRetainsCurrentPlan) {
    // The already-recommended plan for a single-front, single-recommendation scenario: a
    // manual trigger still finds "the same plan" as the best alternative (nothing to gain
    // by switching to an identical route), so it must retain, not report a pointless
    // switch to an equivalent plan.
    SingleLegFixture f = buildSingleLegFixture();
    Mission mission = launchedMission(MissionId(1), f.plan);

    DynamicReplanner replanner;
    PlanId nextPlanId(1000);
    MissionChange manual{.kind = ChangeKind::ManualReplan, .atSec = 5.0};
    const ReplanDecision d = replanner.evaluate(mission, f.vehicle, f.emergency, f.graph, f.model,
                                                5.0, manual, OptimizationProfile::balanced(),
                                                nextPlanId);
    CHECK(d.decision == ReplanDecisionType::Continue);
}

// ---------------------------------------------------------------------------
// Mandatory replan ignores switchMargin entirely (brief §33).
// ---------------------------------------------------------------------------

ORCA_TEST(DynamicReplanner, MandatoryReplanIgnoresSwitchMarginEvenWhenAlternativeIsOnlySlightlyBetter) {
    DiamondFixture f = buildDiamondFixture();
    const MissionPlan currentPlan = f.front.solutions[slowestSolutionIndex(f.front)];
    Mission mission = launchedMission(MissionId(1), currentPlan);

    // An artificially huge switchMargin that would normally suppress any optional switch.
    ReplanPolicy hugeMargin = defaultReplanPolicy();
    hugeMargin.switchMargin = 1000.0;
    DynamicReplanner replanner(hugeMargin);
    PlanId nextPlanId(1000);

    Seconds fastestArrival = 1.0e18;
    for (const auto& sol : f.front.solutions) fastestArrival = std::min(fastestArrival, sol.arrivalTimeSec());
    const Seconds tightDeadline = (fastestArrival + currentPlan.arrivalTimeSec()) / 2.0;
    Emergency tightened(EmergencyId(101), SatelliteId(101), EmergencyKind::PowerFailure,
                       Severity::High, 0.0, tightDeadline);

    MissionChange deadlineChange{.kind = ChangeKind::DeadlineChanged, .atSec = 5.0};
    const ReplanDecision d = replanner.evaluate(mission, f.vehicle, tightened, f.graph, f.model,
                                                5.0, deadlineChange, OptimizationProfile::balanced(),
                                                nextPlanId);
    CHECK(d.decision == ReplanDecisionType::Replan);  // margin never consulted -- mandatory
}

// ---------------------------------------------------------------------------
// Hysteresis / oscillation (brief §41): switching once must not immediately want to switch
// back on the very next identical-ish trigger.
// ---------------------------------------------------------------------------

ORCA_TEST(DynamicReplanner, DoesNotOscillateBetweenTwoNearEqualPlans) {
    DiamondFixture f = buildDiamondFixture();
    const MissionPlan currentPlan = f.front.solutions[slowestSolutionIndex(f.front)];
    Mission mission = launchedMission(MissionId(1), currentPlan);

    DynamicReplanner replanner;
    PlanId nextPlanId(1000);
    MissionChange manual{.kind = ChangeKind::ManualReplan, .atSec = 5.0};

    const ReplanDecision first = replanner.evaluate(mission, f.vehicle, f.emergency, f.graph,
                                                     f.model, 5.0, manual,
                                                     OptimizationProfile::balanced(), nextPlanId);
    CHECK(first.decision == ReplanDecisionType::Replan);
    mission.adoptRoute(*first.newPlan);

    // Immediately ask again, same trigger, same everything -- having just adopted the
    // better plan, there is nothing left to gain by switching again (let alone back).
    const ReplanDecision second = replanner.evaluate(mission, f.vehicle, f.emergency, f.graph,
                                                      f.model, 5.0, manual,
                                                      OptimizationProfile::balanced(), nextPlanId);
    CHECK(second.decision == ReplanDecisionType::Continue);
}

// ---------------------------------------------------------------------------
// Idempotence (brief §42) and determinism (brief §43).
// ---------------------------------------------------------------------------

ORCA_TEST(DynamicReplanner, IdenticalStateAndTriggerProduceTheSameDecisionTwice) {
    SingleLegFixture f = buildSingleLegFixture();
    Mission mission = launchedMission(MissionId(1), f.plan);
    DynamicReplanner replanner;
    PlanId nextPlanId(1000);
    MissionChange trivial{.kind = ChangeKind::FuelChanged, .atSec = 100.0, .magnitudeFraction = 0.001};

    const ReplanDecision a = replanner.evaluate(mission, f.vehicle, f.emergency, f.graph, f.model,
                                                100.0, trivial, OptimizationProfile::balanced(),
                                                nextPlanId);
    const ReplanDecision b = replanner.evaluate(mission, f.vehicle, f.emergency, f.graph, f.model,
                                                100.0, trivial, OptimizationProfile::balanced(),
                                                nextPlanId);
    CHECK(a.decision == b.decision);
    CHECK(a.reason == b.reason);
    CHECK(a.validity.state == b.validity.state);
}

ORCA_TEST(DynamicReplanner, TwoFreshReplannersGivenIdenticalInputsAgree) {
    SingleLegFixture f = buildSingleLegFixture();
    Mission mission = launchedMission(MissionId(1), f.plan);
    MissionChange trivial{.kind = ChangeKind::FuelChanged, .atSec = 100.0, .magnitudeFraction = 0.001};

    DynamicReplanner replannerA;
    PlanId planIdA(1000);
    const ReplanDecision a = replannerA.evaluate(mission, f.vehicle, f.emergency, f.graph, f.model,
                                                 100.0, trivial, OptimizationProfile::balanced(),
                                                 planIdA);

    DynamicReplanner replannerB;
    PlanId planIdB(1000);
    const ReplanDecision b = replannerB.evaluate(mission, f.vehicle, f.emergency, f.graph, f.model,
                                                 100.0, trivial, OptimizationProfile::balanced(),
                                                 planIdB);
    CHECK(a.decision == b.decision);
    CHECK(a.reason == b.reason);
}

// ---------------------------------------------------------------------------
// Mission progress (brief §35): replanning uses CURRENT state, not the mission's start.
// ---------------------------------------------------------------------------

ORCA_TEST(DynamicReplanner, MissionProgressChangesTheReplanningStartPoint) {
    DiamondFixture f = buildDiamondFixture();
    const MissionPlan currentPlan = f.front.solutions[slowestSolutionIndex(f.front)];
    CHECK_EQ(currentPlan.legCount(), std::size_t{2});

    // Not yet started leg 1 -- remaining evaluation covers both legs.
    Mission freshMission = launchedMission(MissionId(1), currentPlan);
    const RemainingRouteEvaluation freshRemaining = evaluateRemainingRouteCost(
        f.graph, f.model, freshMission.currentRoute(), 0, 0.0, permissive());

    // Already committed to leg 0 (progress > 0) -- remaining evaluation covers only leg 1.
    Mission advancedMission = launchedMission(MissionId(2), currentPlan);
    advancedMission.setProgressOnLeg(0.5);
    const std::size_t startLeg =
        remainingLegStartIndex(advancedMission.currentLeg(), advancedMission.progressOnLeg());
    CHECK_EQ(startLeg, std::size_t{1});
    const RemainingRouteEvaluation advancedRemaining = evaluateRemainingRouteCost(
        f.graph, f.model, advancedMission.currentRoute(), startLeg, 0.0, permissive());

    CHECK(advancedRemaining.cost.time < freshRemaining.cost.time);
}

// ---------------------------------------------------------------------------
// Deadline (brief §36): mandatory replan when the deadline can no longer be met; abort
// when no alternative meets it either.
// ---------------------------------------------------------------------------

ORCA_TEST(DynamicReplanner, DeadlineImpossibleForEveryAlternativeAborts) {
    DiamondFixture f = buildDiamondFixture();
    const MissionPlan currentPlan = f.front.solutions[slowestSolutionIndex(f.front)];
    Mission mission = launchedMission(MissionId(1), currentPlan);

    // Tighter than even the fastest alternative's own arrival.
    Seconds fastestArrival = 1.0e18;
    for (const auto& sol : f.front.solutions) fastestArrival = std::min(fastestArrival, sol.arrivalTimeSec());
    Emergency impossible(EmergencyId(101), SatelliteId(101), EmergencyKind::PowerFailure,
                        Severity::High, 0.0, fastestArrival * 0.5);

    DynamicReplanner replanner;
    PlanId nextPlanId(1000);
    MissionChange deadlineChange{.kind = ChangeKind::DeadlineChanged, .atSec = 5.0};
    const ReplanDecision d = replanner.evaluate(mission, f.vehicle, impossible, f.graph, f.model,
                                                5.0, deadlineChange, OptimizationProfile::balanced(),
                                                nextPlanId);
    CHECK(d.decision == ReplanDecisionType::Abort);
    CHECK(d.abortReason == AbortReason::DeadlineImpossible);
}

// ---------------------------------------------------------------------------
// Fuel/delta-v (brief §37): the vehicle's REMAINING resource governs re-evaluation, not
// its original total.
// ---------------------------------------------------------------------------

ORCA_TEST(DynamicReplanner, RemainingDeltaVNotOriginalBudgetGovernsReplanning) {
    SingleLegFixture f = buildSingleLegFixture();
    Mission mission = launchedMission(MissionId(1), f.plan);

    // Same TOTAL budget (5.0) as the original vehicle, but with almost all of it already
    // spent -- if the original total were used instead of the remaining amount, this
    // would incorrectly appear feasible.
    RescueVehicle spent(VehicleId(1), "RV-1", f.vehicle.orbit(), 5.0);
    spent.consumeDeltaV(4.9);
    CHECK_NEAR(spent.deltaVBudgetKms(), 5.0, 1e-9);  // total is unchanged
    CHECK(spent.deltaVRemainingKms() < 0.15);         // but very little is left

    const MissionValidity v =
        evaluateMissionValidity(mission, spent, f.emergency, f.graph, f.model, 0.0);
    CHECK(v.state == ValidityState::Invalid);
    CHECK(v.reason == ReplanReason::DeltaVConstraint);
}

// ---------------------------------------------------------------------------
// Target condition (brief §38): a changed emergency (severity/deadline) is re-evaluated
// against the CURRENT emergency object, not a stale one.
// ---------------------------------------------------------------------------

ORCA_TEST(DynamicReplanner, TargetConditionChangeIsEvaluatedAgainstCurrentEmergencyState) {
    SingleLegFixture f = buildSingleLegFixture();
    Mission mission = launchedMission(MissionId(1), f.plan);

    // A "degraded" emergency: same subject, drastically shorter remaining window --
    // representing new information about the casualty's true state.
    Emergency degraded(EmergencyId(101), SatelliteId(101), EmergencyKind::PowerFailure,
                      Severity::Critical, 0.0, 1000.0);

    DynamicReplanner replanner;
    PlanId nextPlanId(1000);
    MissionChange targetChange{.kind = ChangeKind::TargetConditionChanged, .atSec = 5.0};
    const ReplanDecision d = replanner.evaluate(mission, f.vehicle, degraded, f.graph, f.model,
                                                5.0, targetChange, OptimizationProfile::balanced(),
                                                nextPlanId);
    CHECK(d.validity.state == ValidityState::Invalid);
    CHECK(d.validity.reason == ReplanReason::DeadlineThreat);
}

// ---------------------------------------------------------------------------
// Vehicle availability (brief §39): a disabled vehicle invalidates the mission.
// ---------------------------------------------------------------------------

ORCA_TEST(DynamicReplanner, DisabledVehicleAbortsWhenNoAlternativeVehicleIsOffered) {
    SingleLegFixture f = buildSingleLegFixture();
    Mission mission = launchedMission(MissionId(1), f.plan);

    RescueVehicle disabled(VehicleId(1), "RV-1", f.vehicle.orbit(), 5.0);
    disabled.setStatus(VehicleStatus::Disabled);

    DynamicReplanner replanner;
    PlanId nextPlanId(1000);
    MissionChange unavailable{.kind = ChangeKind::VehicleUnavailable, .atSec = 5.0};
    const ReplanDecision d = replanner.evaluate(mission, disabled, f.emergency, f.graph, f.model,
                                                5.0, unavailable, OptimizationProfile::balanced(),
                                                nextPlanId);
    // DynamicReplanner has no OTHER vehicle to offer (that is GlobalReplanner's job) --
    // the only honest outcome for THIS mission, with THIS vehicle disabled, is Abort.
    CHECK(d.decision == ReplanDecisionType::Abort);
    CHECK(d.abortReason == AbortReason::VehicleUnavailable);
}

// ---------------------------------------------------------------------------
// Communication loss (brief §40): documented policy -- treated as an optional-path
// trigger (always material, since it carries no magnitude), never an automatic abort or
// automatic invalidity by itself. Physics did not change; only the ability to confirm
// state did, so the existing route is re-considered, not discarded outright.
// ---------------------------------------------------------------------------

ORCA_TEST(DynamicReplanner, CommunicationLossReEvaluatesButDoesNotForceInvalidity) {
    SingleLegFixture f = buildSingleLegFixture();
    Mission mission = launchedMission(MissionId(1), f.plan);

    DynamicReplanner replanner;
    PlanId nextPlanId(1000);
    MissionChange commLoss{.kind = ChangeKind::CommunicationLost, .atSec = 5.0};
    const ReplanDecision d = replanner.evaluate(mission, f.vehicle, f.emergency, f.graph, f.model,
                                                5.0, commLoss, OptimizationProfile::balanced(),
                                                nextPlanId);
    // The vehicle/emergency/route are all otherwise unchanged -- validity stays Valid, and
    // since no better alternative exists on this single-edge graph, the decision is
    // Continue, not an automatic Abort just because contact was lost.
    CHECK(d.validity.state == ValidityState::Valid);
    CHECK(d.decision == ReplanDecisionType::Continue);
}

// ---------------------------------------------------------------------------
// Metrics and history (brief §27-28): counters and the log actually move.
// ---------------------------------------------------------------------------

ORCA_TEST(DynamicReplanner, MetricsAndHistoryAccumulateAcrossCalls) {
    SingleLegFixture f = buildSingleLegFixture();
    Mission mission = launchedMission(MissionId(1), f.plan);
    DynamicReplanner replanner;
    PlanId nextPlanId(1000);
    MissionChange trivial{.kind = ChangeKind::FuelChanged, .atSec = 100.0, .magnitudeFraction = 0.001};

    (void)replanner.evaluate(mission, f.vehicle, f.emergency, f.graph, f.model, 100.0, trivial,
                             OptimizationProfile::balanced(), nextPlanId);
    (void)replanner.evaluate(mission, f.vehicle, f.emergency, f.graph, f.model, 200.0, trivial,
                             OptimizationProfile::balanced(), nextPlanId);

    CHECK_EQ(replanner.metrics().replanRequests, 2);
    CHECK_EQ(replanner.metrics().planRetentions, 2);
    CHECK_EQ(replanner.history().size(), std::size_t{2});
    CHECK(replanner.metrics().averageReplanComputationMs() >= 0.0);
}
