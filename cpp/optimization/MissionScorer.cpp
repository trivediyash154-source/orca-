#include "MissionScorer.hpp"

namespace orca {

namespace {

/// Floating-point noise floor for score/objective tie detection — the same role
/// `kDominanceNoiseEpsilon` (`Dominance.hpp`) plays for dominance, kept as its own named
/// constant here rather than a cross-module reuse, since "two scores are the same" and
/// "two cost vectors dominate" are different comparisons that happen to want a similarly
/// small tolerance, not the same one by necessity.
constexpr double kTieEpsilon = 1.0e-9;

[[nodiscard]] bool nearlyEqual(double a, double b) noexcept {
    return (a > b ? a - b : b - a) < kTieEpsilon;
}

/// True if plan `a` should be preferred over plan `b` under `OPTIMIZATION_MODEL.md` §10's
/// tie-break order: lowest score, then lowest risk, then lowest time, then lowest fuel,
/// then lowest PlanId.
[[nodiscard]] bool isPreferred(const ScoreBreakdown& scoreA, const MissionPlan& planA,
                               const ScoreBreakdown& scoreB, const MissionPlan& planB) {
    if (!nearlyEqual(scoreA.totalScore, scoreB.totalScore)) {
        return scoreA.totalScore < scoreB.totalScore;
    }
    if (!nearlyEqual(planA.totalCost().risk, planB.totalCost().risk)) {
        return planA.totalCost().risk < planB.totalCost().risk;
    }
    if (!nearlyEqual(planA.totalCost().time, planB.totalCost().time)) {
        return planA.totalCost().time < planB.totalCost().time;
    }
    if (!nearlyEqual(planA.totalCost().fuel, planB.totalCost().fuel)) {
        return planA.totalCost().fuel < planB.totalCost().fuel;
    }
    return planA.id().value() < planB.id().value();
}

}  // namespace

ScoreBreakdown scorePlan(const ObjectiveVector& cost, const ObjectiveVector& ideal,
                         const ObjectiveVector& nadir, const OptimizationProfile& profile) {
    const NormalizedObjectives normalized = normalize(cost, ideal, nadir);

    // Weights are normalised to sum to 1 here, at point of use -- OptimizationProfile.hpp
    // stores exactly what the caller passed, so this is where "only ratios matter"
    // (OPTIMIZATION_MODEL.md §6.2) actually takes effect. Sum > 0 is an invariant every
    // OptimizationProfile factory/validator already enforces.
    const double weightSum =
        profile.wTime + profile.wFuel + profile.wRisk + profile.wDistance;

    ScoreBreakdown breakdown;
    breakdown.normalized = normalized;
    breakdown.weightedTime = (profile.wTime / weightSum) * normalized.time;
    breakdown.weightedFuel = (profile.wFuel / weightSum) * normalized.fuel;
    breakdown.weightedRisk = (profile.wRisk / weightSum) * normalized.risk;
    breakdown.weightedDistance = (profile.wDistance / weightSum) * normalized.distance;
    breakdown.totalScore = breakdown.weightedTime + breakdown.weightedFuel +
                           breakdown.weightedRisk + breakdown.weightedDistance;
    return breakdown;
}

std::vector<ScoreBreakdown> scoreAllPlans(const ParetoFront& front,
                                          const OptimizationProfile& profile) {
    std::vector<ScoreBreakdown> breakdowns;
    breakdowns.reserve(front.solutions.size());
    for (const MissionPlan& plan : front.solutions) {
        breakdowns.push_back(scorePlan(plan.totalCost(), front.ideal, front.nadir, profile));
    }
    return breakdowns;
}

std::optional<RecommendedPlan> selectRecommendedPlan(const ParetoFront& front,
                                                      const OptimizationProfile& profile) {
    if (front.solutions.empty()) {
        return std::nullopt;
    }

    const std::vector<ScoreBreakdown> scores = scoreAllPlans(front, profile);

    std::size_t bestIndex = 0;
    for (std::size_t i = 1; i < front.solutions.size(); ++i) {
        if (isPreferred(scores[i], front.solutions[i], scores[bestIndex],
                        front.solutions[bestIndex])) {
            bestIndex = i;
        }
    }

    return RecommendedPlan{bestIndex, scores[bestIndex]};
}

}  // namespace orca
