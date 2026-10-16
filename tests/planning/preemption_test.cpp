/// `DynamicReplanner::evaluatePreemption()` tests — Phase 7 brief §23. A pure policy
/// decision: given a mission's progress and the priority gap to a candidate emergency,
/// should this mission be abandoned (freeing its vehicle) in favour of the new one.

#include "../../cpp/domain/RescueVehicle.hpp"
#include "../../cpp/domain/Emergency.hpp"
#include "../../cpp/domain/Mission.hpp"
#include "../../cpp/graph/MissionGraph.hpp"
#include "../../cpp/optimization/CandidateGeneration.hpp"
#include "../../cpp/planning/DynamicReplanner.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {

MissionPlan buildTrivialPlan() {
    MissionGraph g;
    TransferModel model;
    TransferConstraints c;
    c.maxDeltaVKms = 1.0e6;
    c.deltaVBudgetKms = 1.0e6;
    c.availableFuelFraction = 1.0;
    c.maxWaitSec = 1.0e13;
    c.latestArrivalSec = 1.0e18;

    const Orbit v1o = Orbit::fromAltitude(500.0, degToRad(5.0), 0.0, 0.0);
    const Orbit e1o = Orbit::fromAltitude(550.0, degToRad(6.0), degToRad(5.0), degToRad(0.1));
    const NodeId v1n = g.addNode(GraphNode{NodeId{0}, VehicleId(1), v1o});
    const NodeId e1n = g.addNode(GraphNode{NodeId{0}, SatelliteId(101), e1o});
    g.addEdge(v1n, e1n, model.evaluate(v1o, e1o, 0.0, c), 0.0);

    const RescueVehicle rv1(VehicleId(1), "RV-1", v1o, 5.0);
    const Emergency e1(EmergencyId(101), SatelliteId(101), EmergencyKind::PowerFailure,
                       Severity::High, 0.0, 5.0e7);
    PlanId nextPlanId(1);
    EntityNodeIndex index = buildEntityNodeIndex(g);
    CandidateFront front = generateCandidateFront(g, model, index, rv1, e1, 0.0, nextPlanId);
    const std::optional<RecommendedPlan> recommended =
        selectRecommendedPlan(front.front, OptimizationProfile::balanced());
    return front.front.solutions[recommended->planIndex];
}

}  // namespace

ORCA_TEST(Preemption, LowProgressAndLargePriorityGapPreempts) {
    Mission mission(MissionId(1), buildTrivialPlan(), 0.0);
    mission.setState(MissionState::Launched);
    mission.setState(MissionState::InTransit);

    DynamicReplanner replanner;
    const ReplanDecision d = replanner.evaluatePreemption(mission, 0.1, 0.3, 0.9, 20.0);
    CHECK(d.decision == ReplanDecisionType::Abort);
    CHECK(d.reason == ReplanReason::NewHigherPriorityEmergency);
    CHECK(d.abortReason == AbortReason::Preempted);
}

ORCA_TEST(Preemption, HighProgressNeverPreemptsRegardlessOfPriorityGap) {
    Mission mission(MissionId(1), buildTrivialPlan(), 0.0);
    mission.setState(MissionState::Launched);
    mission.setState(MissionState::InTransit);

    DynamicReplanner replanner;
    // The same huge priority gap as the test above, but the mission is almost done.
    const ReplanDecision d = replanner.evaluatePreemption(mission, 0.95, 0.1, 0.99, 20.0);
    CHECK(d.decision == ReplanDecisionType::Continue);
}

ORCA_TEST(Preemption, SmallPriorityGapDoesNotPreemptEvenAtLowProgress) {
    Mission mission(MissionId(1), buildTrivialPlan(), 0.0);
    mission.setState(MissionState::Launched);
    mission.setState(MissionState::InTransit);

    DynamicReplanner replanner;
    const ReplanDecision d = replanner.evaluatePreemption(mission, 0.05, 0.5, 0.55, 20.0);
    CHECK(d.decision == ReplanDecisionType::Continue);
}

ORCA_TEST(Preemption, ExactlyAtProgressCutoffDoesNotPreempt) {
    Mission mission(MissionId(1), buildTrivialPlan(), 0.0);
    mission.setState(MissionState::Launched);
    mission.setState(MissionState::InTransit);

    ReplanPolicy policy = defaultReplanPolicy();
    DynamicReplanner replanner(policy);
    const ReplanDecision d =
        replanner.evaluatePreemption(mission, policy.preemptionMaxProgressFraction, 0.1, 0.9, 20.0);
    CHECK(d.decision == ReplanDecisionType::Continue);
}

ORCA_TEST(Preemption, JustOverThePriorityMarginDoesPreempt) {
    Mission mission(MissionId(1), buildTrivialPlan(), 0.0);
    mission.setState(MissionState::Launched);
    mission.setState(MissionState::InTransit);

    ReplanPolicy policy = defaultReplanPolicy();
    DynamicReplanner replanner(policy);
    // Comfortably over the margin -- exactly-at-the-boundary is a floating-point equality
    // question this test does not need to settle; the policy's own `>=` semantics are
    // documented in ReplanPolicy.hpp, not re-litigated here.
    const ReplanDecision d = replanner.evaluatePreemption(
        mission, 0.0, 0.3, 0.3 + policy.preemptionPriorityMargin + 0.01, 20.0);
    CHECK(d.decision == ReplanDecisionType::Abort);
}

ORCA_TEST(Preemption, RejectsOutOfRangeProgressFraction) {
    Mission mission(MissionId(1), buildTrivialPlan(), 0.0);
    mission.setState(MissionState::Launched);
    mission.setState(MissionState::InTransit);

    DynamicReplanner replanner;
    CHECK_THROWS(replanner.evaluatePreemption(mission, 1.5, 0.1, 0.9, 20.0));
    CHECK_THROWS(replanner.evaluatePreemption(mission, -0.1, 0.1, 0.9, 20.0));
}

ORCA_TEST(Preemption, PreemptionDoesNotRetargetTheMission) {
    // Mission::emergency() has no setter (Mission.hpp) -- preemption must never attempt to
    // mutate it. This test documents that guarantee structurally: the decision carries no
    // new plan at all (a caller preempting a mission ABORTS it; a separate global replan
    // pass is what dispatches the freed vehicle to the new emergency).
    Mission mission(MissionId(1), buildTrivialPlan(), 0.0);
    mission.setState(MissionState::Launched);
    mission.setState(MissionState::InTransit);

    DynamicReplanner replanner;
    const ReplanDecision d = replanner.evaluatePreemption(mission, 0.1, 0.3, 0.9, 20.0);
    CHECK(!d.newPlan.has_value());
    CHECK(mission.emergency().value() == EmergencyId(101).value());  // unchanged
}
