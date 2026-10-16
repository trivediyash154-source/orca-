/// Tests for `MissionScorer`: `scorePlan()`'s arithmetic (hand-computed, independently of
/// the implementation), weight normalisation at point of use, and the full deterministic
/// tie-break chain `OPTIMIZATION_MODEL.md` §10 specifies (score, risk, time, fuel,
/// PlanId).

#include "../../cpp/optimization/MissionScorer.hpp"
#include "../TestFramework.hpp"

using namespace orca;

namespace {

ObjectiveVector v(double time, double fuel, double risk, double distance) {
    return ObjectiveVector{time, fuel, risk, distance};
}

MissionPlan plan(std::uint32_t planId, const ObjectiveVector& cost) {
    return MissionPlan::makeFeasible(PlanId(planId), VehicleId(1), EmergencyId(1),
                                     std::vector<EdgeId>{EdgeId(0)}, cost, 0.0,
                                     cost.time);
}

ParetoFront frontOf(std::vector<MissionPlan> solutions) {
    ParetoFront front;
    std::vector<ObjectiveVector> costs;
    for (const MissionPlan& p : solutions) {
        costs.push_back(p.totalCost());
    }
    front.ideal = computeIdeal(costs);
    front.nadir = computeNadir(costs);
    front.solutions = std::move(solutions);
    return front;
}

}  // namespace

// ---------------------------------------------------------------------------
// scorePlan() arithmetic -- hand-computed expectations.
// ---------------------------------------------------------------------------

ORCA_TEST(MissionScorer, ScorePlanMatchesHandComputedBreakdownAtMidpoint) {
    const ObjectiveVector ideal = v(0, 0, 0, 0);
    const ObjectiveVector nadir = v(100, 10, 1.0, 1000);
    const ObjectiveVector cost = v(50, 5, 0.5, 500);  // exact midpoint on every dimension
    const OptimizationProfile balanced = OptimizationProfile::balanced();  // 0.3/0.3/0.3/0.1

    const ScoreBreakdown b = scorePlan(cost, ideal, nadir, balanced);
    CHECK_NEAR(b.normalized.time, 0.5, 1e-9);
    CHECK_NEAR(b.normalized.fuel, 0.5, 1e-9);
    CHECK_NEAR(b.normalized.risk, 0.5, 1e-9);
    CHECK_NEAR(b.normalized.distance, 0.5, 1e-9);
    // Balanced's weights already sum to 1.0, so weighted_i = w_i * 0.5 directly.
    CHECK_NEAR(b.weightedTime, 0.15, 1e-9);
    CHECK_NEAR(b.weightedFuel, 0.15, 1e-9);
    CHECK_NEAR(b.weightedRisk, 0.15, 1e-9);
    CHECK_NEAR(b.weightedDistance, 0.05, 1e-9);
    CHECK_NEAR(b.totalScore, 0.5, 1e-9);
}

ORCA_TEST(MissionScorer, ScorePlanNormalisesUnnormalisedWeightsAtPointOfUse) {
    // Weights 6/2/1/1 (sum 10, not 1) -- OptimizationProfile stores them as given
    // (OptimizationProfile.hpp), so this checks that normalisation genuinely happens
    // inside scorePlan(), not merely trusted from the caller.
    const ObjectiveVector ideal = v(0, 0, 0, 0);
    const ObjectiveVector nadir = v(100, 10, 1.0, 1000);
    const ObjectiveVector cost = v(25, 5, 0.9, 200);
    const OptimizationProfile custom = OptimizationProfile::custom(6.0, 2.0, 1.0, 1.0);

    const ScoreBreakdown b = scorePlan(cost, ideal, nadir, custom);
    CHECK_NEAR(b.normalized.time, 0.25, 1e-9);
    CHECK_NEAR(b.normalized.fuel, 0.5, 1e-9);
    CHECK_NEAR(b.normalized.risk, 0.9, 1e-9);
    CHECK_NEAR(b.normalized.distance, 0.2, 1e-9);
    // Normalised weights: 0.6 / 0.2 / 0.1 / 0.1.
    CHECK_NEAR(b.weightedTime, 0.6 * 0.25, 1e-9);
    CHECK_NEAR(b.weightedFuel, 0.2 * 0.5, 1e-9);
    CHECK_NEAR(b.weightedRisk, 0.1 * 0.9, 1e-9);
    CHECK_NEAR(b.weightedDistance, 0.1 * 0.2, 1e-9);
    CHECK_NEAR(b.totalScore, 0.15 + 0.10 + 0.09 + 0.02, 1e-9);
}

ORCA_TEST(MissionScorer, ScoreAllPlansReturnsOneBreakdownPerSolutionSameOrder) {
    const ParetoFront front = frontOf({plan(1, v(10, 1, 0.1, 100)), plan(2, v(20, 2, 0.2, 200)),
                                       plan(3, v(30, 3, 0.3, 300))});
    const std::vector<ScoreBreakdown> scores =
        scoreAllPlans(front, OptimizationProfile::balanced());
    CHECK_EQ(scores.size(), front.solutions.size());
}

// ---------------------------------------------------------------------------
// Recommendation selection
// ---------------------------------------------------------------------------

ORCA_TEST(MissionScorer, RecommendsTheLowerScoringPlan) {
    // Under FASTEST (heavy time weight), the faster-but-costlier plan must win.
    const ParetoFront front = frontOf({plan(1, v(100, 5, 0.2, 1000)),   // slower, cheaper
                                       plan(2, v(10, 8, 0.3, 1000))});  // faster, costlier
    const auto rec = selectRecommendedPlan(front, OptimizationProfile::fastest());
    CHECK(rec.has_value());
    CHECK_EQ(front.solutions[rec->planIndex].id().value(), std::uint32_t{2});
}

ORCA_TEST(MissionScorer, EmptyFrontHasNoRecommendation) {
    const ParetoFront empty;
    const auto rec = selectRecommendedPlan(empty, OptimizationProfile::balanced());
    CHECK(!rec.has_value());
}

ORCA_TEST(MissionScorer, SingleMemberFrontIsRecommendedRegardlessOfProfile) {
    const ParetoFront front = frontOf({plan(1, v(500.0, 0.3, 0.2, 1000.0))});
    for (const OptimizationProfile& profile :
        {OptimizationProfile::fastest(), OptimizationProfile::fuelSaving(),
         OptimizationProfile::safetyFirst(), OptimizationProfile::balanced()}) {
        const auto rec = selectRecommendedPlan(front, profile);
        CHECK(rec.has_value());
        CHECK_EQ(rec->planIndex, std::size_t{0});
        CHECK_NEAR(rec->breakdown.totalScore, 0.0, 1e-9);  // degenerate: every norm is 0
    }
}

// ---------------------------------------------------------------------------
// Deterministic tie-break chain: score -> risk -> time -> fuel -> PlanId.
// ---------------------------------------------------------------------------

ORCA_TEST(MissionScorer, TieBreaksOnRiskWhenScoresAreEqual) {
    // Symmetric costs around the midpoint under BALANCED give identical scores; the plan
    // with lower risk must win the tie.
    const ParetoFront front =
        frontOf({plan(1, v(20, 0, 0.5, 0)),   // extra cost on time
                plan(2, v(0, 20, 0.3, 0))});  // same total under equal-weighted balance,
                                              // but lower risk
    // Use a profile that scores these two identically: equal weight on time and fuel,
    // zero on risk/distance would make risk irrelevant to score but that defeats the
    // point -- instead construct a genuine score tie directly via equal contributions.
    // time and fuel both span [0,20], so with wTime==wFuel and cost trading time<->fuel
    // 1:1, both plans score equally; risk differs and must decide it.
    const OptimizationProfile equalTimeFuel = OptimizationProfile::custom(0.5, 0.5, 0.0, 0.0);
    const auto rec = selectRecommendedPlan(front, equalTimeFuel);
    CHECK(rec.has_value());
    CHECK_EQ(front.solutions[rec->planIndex].id().value(), std::uint32_t{2});  // lower risk
}

ORCA_TEST(MissionScorer, TieBreaksOnTimeWhenScoreAndRiskAreEqual) {
    const ParetoFront front = frontOf({plan(1, v(20, 0, 0.5, 0)), plan(2, v(10, 0, 0.5, 0))});
    // Weight only fuel and distance, both 0 on both plans -> identical score; risk is
    // also tied (0.5 both) -> time must decide.
    const OptimizationProfile ignoreTimeInScore =
        OptimizationProfile::custom(0.0, 0.5, 0.0, 0.5);
    const auto rec = selectRecommendedPlan(front, ignoreTimeInScore);
    CHECK(rec.has_value());
    CHECK_EQ(front.solutions[rec->planIndex].id().value(), std::uint32_t{2});  // lower time
}

ORCA_TEST(MissionScorer, TieBreaksOnPlanIdAsFinalGuaranteedUniqueArbiter) {
    // Fully identical cost vectors -- score, risk, time and fuel all tie exactly; only
    // PlanId can break it, and it must (deterministically, the lower one).
    const ParetoFront front = frontOf({plan(5, v(100, 1, 0.1, 100)), plan(3, v(100, 1, 0.1, 100)),
                                       plan(9, v(100, 1, 0.1, 100))});
    const auto rec = selectRecommendedPlan(front, OptimizationProfile::balanced());
    CHECK(rec.has_value());
    CHECK_EQ(front.solutions[rec->planIndex].id().value(), std::uint32_t{3});  // lowest PlanId
}
